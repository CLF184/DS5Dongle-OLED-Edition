#!/usr/bin/env python3
"""Log the dongle's state every few seconds during a play session.

  scripts/session_monitor.py LOGFILE [INTERVAL_S]

Each line: timestamp, per-interval rates from the 0xF5 audio counters and the
0xFD trigger-flow counters, the processes holding the dongle's hidraw node
(game = native path, steam = Steam Input) and the PipeWire streams feeding the
dongle's sink. Game launches/exits are detected automatically (Steam via the
SteamGameId environment variable, anything else via its Windows .exe under
Wine/Proton) and logged as GAME START / GAME END; each line is tagged with the
running game. Stop with Ctrl-C (or kill).
"""
import fcntl, glob, os, struct, subprocess, sys, time

F5 = ["usb_frames", "bt_out", "drop_raw", "drop_enc", "drop_hap",
      "c1_loops", "c1_enc", "raw_push", "hap_push", "mic_frames", "mic_plc",
      "flash_wr", "flash_fail", "setfeat_n", "setfeat_last"]


def find_dongle():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(os.path.join(d, "device/uevent")).read().upper()
        except OSError:
            continue
        if "054C" in ue and ("0CE6" in ue or "0DF2" in ue):  # DualSense or Edge
            return "/dev/" + os.path.basename(d)
    return None


def get_feature(dev, report_id, size):
    buf = bytearray(size); buf[0] = report_id
    with open(dev, "rb+", buffering=0) as f:
        fcntl.ioctl(f, 0xC0004807 | (size << 16), buf)
    return buf


def sample(dev):
    f5 = dict(zip(F5, struct.unpack_from("<15I", get_feature(dev, 0xF5, 61), 1)))
    fd = get_feature(dev, 0xFD, 45)
    bt31, = struct.unpack_from("<I", fd, 1)
    out02, trig, to_bt = struct.unpack_from("<3I", fd, 33)
    f5.update(bt31=bt31, out02=out02, trig=trig, to_bt=to_bt)
    return f5


def hidraw_holders(dev):
    name = os.path.basename(dev)
    who = set()
    for p in glob.glob("/proc/[0-9]*"):
        try:
            if any(os.readlink(os.path.join(p, "fd", fd)).endswith(name)
                   for fd in os.listdir(os.path.join(p, "fd"))):
                cmd = open(os.path.join(p, "cmdline"), "rb").read().split(b"\0")[0]
                who.add(os.path.basename(cmd.decode(errors="replace").replace("\\", "/")))
        except OSError:
            continue
    return ",".join(sorted(who)) or "-"


WINE_SYSTEM = {"winedevice.exe", "services.exe", "explorer.exe", "plugplay.exe",
               "svchost.exe", "rpcss.exe", "winedbg.exe", "steam.exe", "conhost.exe",
               "tabtip.exe", "start.exe", "wineboot.exe", "rundll32.exe",
               "crashreportclient.exe", "epicwebhelper.exe", "unrealcefsubprocess.exe",
               "xalia.exe", "upc.exe", "uplaywebcore.exe", "uplayservice.exe"}


def steam_name(appid):
    for root in ("~/.steam/root/steamapps", "~/.local/share/Steam/steamapps"):
        f = os.path.expanduser(f"{root}/appmanifest_{appid}.acf")
        try:
            for line in open(f):
                if '"name"' in line:
                    return line.split('"')[3]
        except OSError:
            continue
    return f"steam:{appid}"


def current_game():
    """(key, name) of the running game, or (None, None)."""
    exe = None
    for p in glob.glob("/proc/[0-9]*"):
        try:
            cmd = open(os.path.join(p, "cmdline"), "rb").read().split(b"\0")[0].decode(errors="replace")
        except OSError:
            continue
        base = cmd.replace("\\", "/").rsplit("/", 1)[-1]
        if not base.lower().endswith(".exe") or base.lower() in WINE_SYSTEM:
            continue
        try:
            env = open(os.path.join(p, "environ"), "rb").read().split(b"\0")
        except OSError:
            env = []
        for e in env:
            if e.startswith(b"SteamGameId="):
                appid = e.split(b"=", 1)[1].decode()
                if appid.isdigit() and appid != "0":
                    return appid, steam_name(appid)
        exe = exe or base
    return (exe, exe) if exe else (None, None)


def dongle_streams():
    try:
        out = subprocess.run(["pactl", "list", "sink-inputs"], capture_output=True,
                             text=True, timeout=3).stdout
        sinks = subprocess.run(["pactl", "list", "short", "sinks"], capture_output=True,
                               text=True, timeout=3).stdout
    except (OSError, subprocess.TimeoutExpired):
        return "?"
    ids = {l.split()[0] for l in sinks.splitlines() if "DualSense" in l}
    names, cur_sink = [], None
    for line in out.splitlines():
        s = line.strip()
        if s.startswith("Sink:"):
            cur_sink = s.split()[1]
        elif s.startswith("application.name =") and cur_sink in ids:
            names.append(s.split("=", 1)[1].strip().strip('"'))
    return ",".join(names) or "-"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    log, interval = sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
    prev, prev_t = None, None
    game_key, game_name = None, None
    with open(log, "a", buffering=1) as out:
        out.write(f"# session start {time.strftime('%F %T')}\n")
        while True:
            key, name = current_game()
            if key != game_key:
                if game_key:
                    out.write(f"{time.strftime('%T')} GAME END: {game_name}\n")
                if key:
                    out.write(f"{time.strftime('%T')} GAME START: {name}\n")
                game_key, game_name = key, name
            dev = find_dongle()
            now = time.time()
            if dev is None:
                out.write(f"{time.strftime('%T')} DONGLE NOT ENUMERATED\n")
                prev = None
            else:
                try:
                    s = sample(dev)
                except OSError as e:
                    out.write(f"{time.strftime('%T')} read error: {e}\n")
                    time.sleep(interval); continue
                if prev is not None:
                    dt = now - prev_t
                    r = lambda k: (s[k] - prev[k]) / dt
                    out.write(
                        f"{time.strftime('%T')} [{game_name or 'desktop'}] usb/s={r('usb_frames'):.0f} bt_out/s={r('bt_out'):.1f} "
                        f"drop r/e/h=+{s['drop_raw']-prev['drop_raw']}/+{s['drop_enc']-prev['drop_enc']}/"
                        f"+{s['drop_hap']-prev['drop_hap']} c1={'alive' if s['c1_loops'] != prev['c1_loops'] else 'FROZEN'} "
                        f"in31/s={r('bt31'):.0f} out02/s={r('out02'):.1f} trig/s={r('trig'):.1f} "
                        f"mic/s={r('mic_frames'):.0f} flash_fail={s['flash_fail']} "
                        f"setfeat_last={s['setfeat_last']:#x} holders={hidraw_holders(dev)} "
                        f"streams={dongle_streams()}\n")
                prev, prev_t = s, now
            time.sleep(interval)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
