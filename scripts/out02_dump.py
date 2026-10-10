#!/usr/bin/env python3
"""Dump the last host 0x02 output report(s) the dongle received (0xF3/0xF4),
decoded field by field (SetStateData layout from src/utils.h)."""
import fcntl, glob, os, sys

def find_dongle():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        ue = open(os.path.join(d, "device/uevent")).read().upper()
        if "054C" in ue and "0CE6" in ue:
            return "/dev/" + os.path.basename(d)
    sys.exit("dongle not found")

def get(dev, rid):
    buf = bytearray(48); buf[0] = rid
    with open(dev, "rb+", buffering=0) as f:
        fcntl.ioctl(f, 0xC0004807 | (48 << 16), buf)
    return bytes(buf[1:])

def decode(s):
    f0, f1 = s[0], s[1]
    flags0 = ["RumbleEmu", "UseRumbleNotHaptics", "AllowRightTrigFFB", "AllowLeftTrigFFB",
              "AllowHpVol", "AllowSpkVol", "AllowMicVol", "AllowAudioCtl"]
    flags1 = ["AllowMuteLight", "AllowAudioMute", "AllowLed", "ResetLights",
              "AllowPlayerInd", "AllowHapticLPF", "AllowMotorPower", "AllowAudioCtl2"]
    print("flags0:", ", ".join(n for i, n in enumerate(flags0) if f0 >> i & 1) or "-")
    print("flags1:", ", ".join(n for i, n in enumerate(flags1) if f1 >> i & 1) or "-")
    print(f"rumble R/L={s[2]}/{s[3]}  vol hp/spk/mic={s[4]}/{s[5]}/{s[6]}  audioctl={s[7]:#04x}")
    print("R2 FFB [10..20]:", s[10:21].hex(" "))
    print("L2 FFB [21..31]:", s[21:32].hex(" "))
    print(f"motor power [36]={s[36]:#04x} (trigger reduce={s[36] & 0xF}, rumble reduce={s[36] >> 4})  audioctl2 [37]={s[37]:#04x}")
    print(f"[38]={s[38]:#04x} (ImprovedRumble={s[38] >> 2 & 1})  LED rgb={s[44]},{s[45]},{s[46]}")

dev = find_dongle()
for rid, label in ((0xF4, "last WITH trigger FFB"), (0xF3, "last of any kind")):
    s = get(dev, rid)
    print(f"== {label}: {s.hex(' ')}")
    decode(s)
