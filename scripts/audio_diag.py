#!/usr/bin/env python3
"""Read the dongle's audio pipeline counters (0xF5 feature report) over hidraw.

  scripts/audio_diag.py            one snapshot
  scripts/audio_diag.py SECONDS    two snapshots SECONDS apart, plus deltas
"""
import fcntl, glob, os, struct, sys, time

FIELDS = ["usb_frames", "bt_out", "drop_raw", "drop_enc", "drop_hap",
          "c1_loops", "c1_enc", "raw_push", "hap_push", "mic_frames", "mic_plc",
          "flash_wr", "flash_fail", "setfeat_n", "setfeat_last"]
# Then core1's health: [60] flags (1 started, 2 speaker encoder failed, 4 mic
# decoder failed, 0x80 = this layout), [61..62] u16 free heap at core1 start in
# KiB. Firmware 0.9.4 to 0.9.5-dev.5 had no 0x80 bit and no heap (it fell off
# the 63-byte reply).
SIZE = 1 + 63

def find_dongle():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(os.path.join(d, "device/uevent")).read().upper()
        except OSError:
            continue
        if "054C" in ue and ("0CE6" in ue or "0DF2" in ue):  # DualSense or Edge
            return "/dev/" + os.path.basename(d)
    sys.exit("dongle hidraw not found")

def query(dev):
    buf = bytearray(SIZE); buf[0] = 0xF5
    hidiocgfeature = 0xC0004807 | (SIZE << 16)  # _IOC(RW, 'H', 0x07, SIZE)
    with open(dev, "rb+", buffering=0) as f:
        fcntl.ioctl(f, hidiocgfeature, buf)
    out = dict(zip(FIELDS, struct.unpack_from("<%dI" % len(FIELDS), buf, 1)))
    flags = buf[61]
    out["c1_flags"] = flags & 0x7F
    out["c1_free_kib"] = struct.unpack_from("<H", buf, 62)[0] if flags & 0x80 else None
    return out

dev = find_dongle()
a = query(dev)
if len(sys.argv) < 2:
    for k in FIELDS: print(f"{k:11} {a[k]:#x}" if k == "setfeat_last" else f"{k:11} {a[k]}")
    print(f"{'c1_flags':11} {a['c1_flags']:#x}")
    print(f"{'c1_free_kib':11} {'n/a' if a['c1_free_kib'] is None else a['c1_free_kib']}")
    sys.exit()
secs = float(sys.argv[1]); time.sleep(secs); b = query(dev)
print(f"{'field':11} {'before':>10} {'after':>10} {'per s':>9}")
for k in FIELDS:
    if k == "setfeat_last": print(f"{k:11} {a[k]:>#10x} {b[k]:>#10x}"); continue
    print(f"{k:11} {a[k]:>10} {b[k]:>10} {(b[k]-a[k])/secs:>9.1f}")
print(f"{'c1_flags':11} {a['c1_flags']:>#10x} {b['c1_flags']:>#10x}")
