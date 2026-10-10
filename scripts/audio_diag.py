#!/usr/bin/env python3
"""Read the dongle's audio pipeline counters (0xF5 feature report) over hidraw.

  scripts/audio_diag.py            one snapshot
  scripts/audio_diag.py SECONDS    two snapshots SECONDS apart, plus deltas
"""
import fcntl, glob, os, struct, sys, time

FIELDS = ["usb_frames", "bt_out", "drop_raw", "drop_enc", "drop_hap",
          "c1_loops", "c1_enc", "raw_push", "hap_push", "mic_frames", "mic_plc"]
SIZE = 1 + 4 * len(FIELDS)

def find_dongle():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(os.path.join(d, "device/uevent")).read().upper()
        except OSError:
            continue
        if "054C" in ue and "0CE6" in ue:
            return "/dev/" + os.path.basename(d)
    sys.exit("dongle hidraw not found")

def query(dev):
    buf = bytearray(SIZE); buf[0] = 0xF5
    hidiocgfeature = 0xC0004807 | (SIZE << 16)  # _IOC(RW, 'H', 0x07, SIZE)
    with open(dev, "rb+", buffering=0) as f:
        fcntl.ioctl(f, hidiocgfeature, buf)
    return dict(zip(FIELDS, struct.unpack_from("<%dI" % len(FIELDS), buf, 1)))

dev = find_dongle()
a = query(dev)
if len(sys.argv) < 2:
    for k in FIELDS: print(f"{k:11} {a[k]}")
    sys.exit()
secs = float(sys.argv[1]); time.sleep(secs); b = query(dev)
print(f"{'field':11} {'before':>10} {'after':>10} {'per s':>9}")
for k in FIELDS:
    print(f"{k:11} {a[k]:>10} {b[k]:>10} {(b[k]-a[k])/secs:>9.1f}")
