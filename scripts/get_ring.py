#!/usr/bin/env python3
"""Show the last 16 host GET_REPORT (feature) requests the dongle answered (0xF2)."""
import fcntl, glob, os, struct, sys
for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
    ue = open(os.path.join(d, "device/uevent")).read().upper()
    if "054C" in ue and ("0CE6" in ue or "0DF2" in ue):  # DualSense or Edge
        dev = "/dev/" + os.path.basename(d)
buf = bytearray(38); buf[0] = 0xF2
with open(dev, "rb+", buffering=0) as f:
    fcntl.ioctl(f, 0xC0004807 | (38 << 16), buf)
total, = struct.unpack_from("<I", buf, 1); nxt = buf[5]; ring = buf[6:38]
print(f"total GET_REPORTs: {total}")
for k in range(16):
    i = (nxt + k) % 16
    rid, n = ring[2 * i], ring[2 * i + 1]
    if rid or n: print(f"  id {rid:#04x} -> {n} bytes{'  (STALL/empty)' if n == 0 else ''}")
