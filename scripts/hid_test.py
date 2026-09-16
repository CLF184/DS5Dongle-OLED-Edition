#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DS5 抓取工具：原始 HID 报告 + Windows.Gaming.Input (WGI) 解析值对比
================================================================
用途
----
验证"幽灵手柄"问题（foundation-sunshine #1056 同类）：
  原始报告里 L2/R2 字节 = 0（正确），但 WGI 静止读数 RightTrigger≈0.5 / RightThumbstickY≈1.0
  → 说明 HID 解析错位，Windows 手柄模板按错误的槽位重读了字节。

支持两种接入方式
----------------
  1. DualSense 直插 USB（VID=054C PID=0CE6）
  2. 经 DS5Dongle / OLED 版加密狗（同样的 VID/PID，因为 dongle 伪装成 DS5）

依赖（Windows 上，需手动安装）
------------------------------
  pip install pywinusb
  pip install winrt-runtime winrt-Windows.Foundation
  pip install winrt-Windows.Foundation.Collections winrt-Windows.Gaming.Input

用法
----
  python ds5_capture.py [--seconds 5] [--vid 054C] [--pid 0CE6]
  python ds5_capture.py --seconds 10            # 抓 10 秒
  python ds5_capture.py --raw-only              # 只抓原始 HID，不读 WGI
"""

import argparse
import sys
import threading
import time


# ---- pywinusb ----
try:
    from pywinusb import hid as phid
except ImportError as _err:
    sys.exit(
        "pywinusb 导入失败，真实原因:\n"
        f"  {type(_err).__name__}: {_err}\n\n"
        "请先安装:  python -m pip install pywinusb\n"
    )


# ---- WGI (Windows.Gaming.Input) ----
# 只用 winrt-* 子包；按依赖顺序导入，保证 Foundation / Collections 先就位。
USE_WGI = True
Gamepad = None

try:
    import winrt.windows.foundation  # noqa: F401
    import winrt.windows.foundation.collections  # noqa: F401
    from winrt.windows.gaming.input import Gamepad
except ImportError as _err:
    print(
        "[依赖] WGI 不可用，将只抓原始 HID。\n"
        f"  原因: {type(_err).__name__}: {_err}\n"
        "  如需 WGI 对比，请安装:\n"
        "    python -m pip install winrt-runtime winrt-Windows.Foundation "
        "winrt-Windows.Foundation.Collections winrt-Windows.Gaming.Input\n"
    )
    Gamepad = None
    USE_WGI = False
else:
    # 有些投影版本把 Gamepad 类挂在模块下，而不是 import 直接返回类。
    if not hasattr(Gamepad, "gamepads") and not hasattr(Gamepad, "Gamepads"):
        cls = getattr(Gamepad, "Gamepad", None)
        if cls is not None:
            Gamepad = cls


DS5_VID = 0x054C
DS5_PID = 0x0CE6

_WGI_DIAG_PRINTED = False


# ----------------------------------------------------------------------
# WGI 读取
# ----------------------------------------------------------------------
def _iter_winrt_vector(vec):
    """尝试将 WinRT IVectorView 转为 Python 列表。"""
    if vec is None:
        return []
    try:
        return list(vec)
    except TypeError:
        pass
    for size_attr in ("size", "Size"):
        n = getattr(vec, size_attr, None)
        if n is not None:
            for get_attr in ("get_at", "GetAt"):
                get_fn = getattr(vec, get_attr, None)
                if callable(get_fn):
                    try:
                        return [get_fn(i) for i in range(n)]
                    except Exception:
                        pass
    try:
        n = len(vec)
        return [vec[i] for i in range(n)]
    except Exception:
        pass
    return []


def _get_gamepads():
    """返回 (来源说明, gamepads 列表) 或 (None, None)。"""
    global _WGI_DIAG_PRINTED
    if Gamepad is None:
        return None, None

    candidates = []

    try:
        v = getattr(Gamepad, "gamepads", None)
        if v is not None and not isinstance(v, property):
            candidates.append(("Gamepad.gamepads", v))
    except Exception as e:
        if not _WGI_DIAG_PRINTED:
            print(f"[WGI 诊断] Gamepad.gamepads 访问失败: {type(e).__name__}: {e}")

    try:
        v = getattr(type(Gamepad), "gamepads", None)
        if v is not None and not isinstance(v, property):
            candidates.append(("type(Gamepad).gamepads", v))
    except Exception as e:
        if not _WGI_DIAG_PRINTED:
            print(f"[WGI 诊断] type(Gamepad).gamepads 访问失败: {type(e).__name__}: {e}")

    for nm in ("GetGamepads", "get_gamepads"):
        fn = getattr(Gamepad, nm, None)
        if callable(fn):
            try:
                v = fn()
                if v is not None:
                    candidates.append((nm, v))
            except Exception as e:
                if not _WGI_DIAG_PRINTED:
                    print(f"[WGI 诊断] {nm}() 失败: {type(e).__name__}: {e}")

    for name, vec in candidates:
        pads = _iter_winrt_vector(vec)
        if pads:
            return name, pads

    if not _WGI_DIAG_PRINTED:
        _WGI_DIAG_PRINTED = True
        print("[WGI 诊断] 未找到可用的 gamepads 枚举通道")
        print("[WGI 诊断] Gamepad 属性:",
              [a for a in dir(Gamepad) if not a.startswith("_")])
        print("[WGI 诊断] Gamepad 元类属性:",
              [a for a in dir(type(Gamepad)) if not a.startswith("_")])
    return None, None


def wgi_read():
    """读 Windows.Gaming.Input 的 Gamepad 读数。返回列表或 None。"""
    if not USE_WGI or Gamepad is None:
        return None
    try:
        src, pads = _get_gamepads()
        if not pads:
            return None

        def _g(o, *names, default=0.0):
            for nm in names:
                v = getattr(o, nm, None)
                if v is not None:
                    return float(v)
            return default

        out = []
        for pad in pads:
            read_fn = getattr(pad, "get_current_reading", None) or \
                      getattr(pad, "GetCurrentReading", None)
            if read_fn is None:
                continue
            r = read_fn()
            if r is None:
                continue
            out.append({
                "LT": round(_g(r, "left_trigger", "LeftTrigger"), 4),
                "RT": round(_g(r, "right_trigger", "RightTrigger"), 4),
                "LX": round(_g(r, "left_thumbstick_x", "LeftThumbstickX"), 4),
                "LY": round(_g(r, "left_thumbstick_y", "LeftThumbstickY"), 4),
                "RX": round(_g(r, "right_thumbstick_x", "RightThumbstickX"), 4),
                "RY": round(_g(r, "right_thumbstick_y", "RightThumbstickY"), 4),
            })
        return out if out else None
    except Exception as e:
        print(f"[WGI 错误] {type(e).__name__}: {e}")
        return None


# ----------------------------------------------------------------------
# 原始 HID
# ----------------------------------------------------------------------
def find_ds5(vid, pid):
    all_devices = phid.HidDeviceFilter(vendor_id=vid, product_id=pid).get_devices()
    if not all_devices:
        print(f"未找到 VID={vid:04X} PID={pid:04X} 设备")
        return []
    print(f"枚举到 {len(all_devices)} 个 HID 设备:")
    for i, d in enumerate(all_devices):
        print(f"  #{i}: {d.product_name or '(unnamed)'}")
        print(f"      path: {d.device_path}")
    return all_devices


def parse_report(data):
    """解析 DS5 输入报告字节: [0]=report id, [1]=LX [2]=LY [3]=RX [4]=RY [5]=L2 [6]=R2"""
    if len(data) < 7:
        return None
    return {
        "LX": data[1], "LY": data[2], "RX": data[3], "RY": data[4],
        "L2": data[5], "R2": data[6],
        "head": " ".join(f"{b:02x}" for b in data[:8]),
    }


# ----------------------------------------------------------------------
# main
# ----------------------------------------------------------------------
def main():
    global USE_WGI

    ap = argparse.ArgumentParser(description="DS5 原始 HID + WGI 对比抓取")
    ap.add_argument("--seconds", type=int, default=5, help="抓取时长(秒), 默认 5")
    ap.add_argument("--vid", type=lambda x: int(x, 16), default=DS5_VID)
    ap.add_argument("--pid", type=lambda x: int(x, 16), default=DS5_PID)
    ap.add_argument("--raw-only", action="store_true", help="只抓原始 HID, 不读 WGI")
    args = ap.parse_args()

    if args.raw_only:
        USE_WGI = False

    devs = find_ds5(args.vid, args.pid)
    if not devs:
        return 1

    opened = []
    for d in devs:
        try:
            d.open()
            opened.append(d)
        except Exception as e:
            print(f"  打开失败: {e}")

    if not opened:
        print("所有设备都打开失败（可能被其他程序占用）")
        return 1

    target = None
    target_len = 0
    for d in opened:
        caps = d.hid_caps
        ilen = caps.input_report_byte_length if caps else 0
        usage = f"{caps.usage_page:04X}:{caps.usage:04X}" if caps else "?"
        name = d.product_name or ""
        print(f"  [{ilen}字节输入] Usage={usage}  {name}")
        if ilen >= target_len:
            target, target_len = d, ilen

    for d in opened:
        if d is not target:
            try:
                d.close()
            except Exception:
                pass

    caps = target.hid_caps
    print(f"\n打开主接口: {target.product_name}")
    print(f"VID:PID={args.vid:04X}:{args.pid:04X}  Usage={caps.usage_page:04X}:{caps.usage:04X}"
          f"  输入报告={caps.input_report_byte_length}字节")

    samples = []
    lock = threading.Lock()

    def on_data(raw_data):
        data = bytes(raw_data)
        if data:
            with lock:
                samples.append(data)

    target.set_raw_data_handler(on_data)

    print(f"\n抓取 {args.seconds} 秒（手柄静止，勿操作）...")
    print(f"{'时间':<6} {'原始报告(前8字节)':<28} "
          f"{'LX':>3} {'LY':>3} {'RX':>3} {'RY':>3} {'L2':>3} {'R2':>3}   "
          f"WGI(LT/RT/LX/LY/RX/RY)")
    print("-" * 130)

    start = time.time()
    last_line_t = 0
    raw_samples = []
    wgi_samples = []
    while time.time() - start < args.seconds:
        now = time.time()
        with lock:
            if samples and now - last_line_t >= 1.0:
                last_line_t = now
                data = samples[-1]
                parsed = parse_report(data)
                raw_samples.append(parsed)
                w = ""
                if not args.raw_only:
                    wgi = wgi_read()
                    if wgi:
                        wgi_samples.append(wgi)
                        r = wgi[0]
                        w = (f"  WGI LT={r['LT']} RT={r['RT']} "
                             f"LX={r['LX']} LY={r['LY']} "
                             f"RX={r['RX']} RY={r['RY']}")
                if parsed:
                    print(
                        f"{int(now-start):<6} {parsed['head']:<28} "
                        f"{parsed['LX']:>3} {parsed['LY']:>3} {parsed['RX']:>3} "
                        f"{parsed['RY']:>3} {parsed['L2']:>3} {parsed['R2']:>3} {w}"
                    )
        time.sleep(0.01)

    target.close()

    print("\n" + "=" * 60)
    print("汇总")
    print("=" * 60)
    if not raw_samples:
        print("未抓到任何报告！")
        return 1

    print("原始报告（最后 3 帧）:")
    for s in raw_samples[-3:]:
        print(f"  {s['head']}  LX={s['LX']:02X} LY={s['LY']:02X} RX={s['RX']:02X} "
              f"RY={s['RY']:02X} L2={s['L2']:02X} R2={s['R2']:02X}")

    if wgi_samples:
        r = wgi_samples[-1][0]
        print(f"WGI 最后读数: LT={r['LT']} RT={r['RT']} "
              f"LX={r['LX']} LY={r['LY']} RX={r['RX']} RY={r['RY']}")
    else:
        print("WGI 未取到读数。")

    return 0


if __name__ == "__main__":
    sys.exit(main())