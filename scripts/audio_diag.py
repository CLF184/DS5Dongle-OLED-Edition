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
# decoder failed, bits 3..6 core1's start-up stage, 0x80 = this layout),
# [61..62] u16 free heap at core1 start in KiB. Firmware 0.9.4 to 0.9.5-dev.5
# had no 0x80 bit, no stage and no heap (it fell off the 63-byte reply).
# The stage is the last start-up step core1 reached (src/audio.cpp); a core1
# stuck at stage N stopped in the step after it.
STAGES = {
    0: "never entered core1_entry",
    1: "entered; stopped registering for flash lockout",
    2: "flash lockout registered; stopped starting its cycle counter",
    3: "cycle counter on; stopped reading the free heap",
    4: "stopped creating the speaker encoder",
    5: "speaker encoder created; stopped setting it / the resampler up",
    6: "speaker ready; stopped before the mic decoder",
    7: "stopped creating the mic decoder",
    8: "mic decoder created; stopped before its loop",
    9: "running its loop",
}
SIZE = 1 + 63

def stage_text(st):
    return "n/a" if st is None else "%d (%s)" % (st, STAGES.get(st, "unknown"))

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
    out["c1_flags"] = flags & 0x07
    out["c1_stage"] = (flags >> 3) & 0x0F if flags & 0x80 else None
    out["c1_free_kib"] = struct.unpack_from("<H", buf, 62)[0] if flags & 0x80 else None
    return out

dev = find_dongle()
a = query(dev)
if len(sys.argv) < 2:
    for k in FIELDS: print(f"{k:11} {a[k]:#x}" if k == "setfeat_last" else f"{k:11} {a[k]}")
    print(f"{'c1_flags':11} {a['c1_flags']:#x}")
    print(f"{'c1_free_kib':11} {'n/a' if a['c1_free_kib'] is None else a['c1_free_kib']}")
    print(f"{'c1_stage':11} {stage_text(a['c1_stage'])}")
    sys.exit()
secs = float(sys.argv[1]); time.sleep(secs); b = query(dev)
print(f"{'field':11} {'before':>10} {'after':>10} {'per s':>9}")
for k in FIELDS:
    if k == "setfeat_last": print(f"{k:11} {a[k]:>#10x} {b[k]:>#10x}"); continue
    print(f"{k:11} {a[k]:>10} {b[k]:>10} {(b[k]-a[k])/secs:>9.1f}")
print(f"{'c1_flags':11} {a['c1_flags']:>#10x} {b['c1_flags']:>#10x}")
print(f"{'c1_stage':11} {stage_text(b['c1_stage'])}")
