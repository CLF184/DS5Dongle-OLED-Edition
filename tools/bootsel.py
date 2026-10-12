#!/usr/bin/env python3
"""Put a connected dongle into BOOTSEL (flashing) mode over its HID interface.

Sends the configurator's reboot-to-BOOTSEL command (feature report 0xF6,
function 0x08) to the dongle's hidraw node. Works while the app holds the
vendor interface. Used by tools/dev_firmware.sh --flash.
"""
import fcntl
import glob
import os
import sys


def hidiocsfeature(n):
    return (3 << 30) | (n << 16) | (ord("H") << 8) | 0x06


for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
    uevent = open(d + "/device/uevent").read()
    # The dongle on USB (bus 0003) with a DualSense / Edge ID; never a real controller over Bluetooth.
    if "HID_ID=0003:0000054C:00000CE6" not in uevent and "HID_ID=0003:0000054C:00000DF2" not in uevent:
        continue
    dev = "/dev/" + os.path.basename(d)
    buf = bytearray(64)
    buf[0], buf[1] = 0xF6, 0x08
    fd = os.open(dev, os.O_RDWR)
    try:
        fcntl.ioctl(fd, hidiocsfeature(len(buf)), bytes(buf))
        print("BOOTSEL sent via", dev)
    finally:
        os.close(fd)
    sys.exit(0)
print("No dongle found on USB (is a controller connected? It only appears then)", file=sys.stderr)
sys.exit(1)
