#!/usr/bin/env python3
# patch_hwc_usage.py — 0x1800 → 0x1000 i libhybris-hwcomposerwindow.so.1.0.0
#
# Baggrund (DOK §5.15): vinduets default-buffer-usage HW_COMPOSER|HW_FB (0x1800)
# fik PVR-grallocen til at fejle; kun HW_FB (0x1000) virkede. Patcher de to
# Thumb-2-instruktioner (encodings genereret med GAS og verificeret på boks 1):
#   mov.w r2, #0x1800  4f f4 c0 52  →  4f f4 80 52  (0x1000)
#   orr.w r5, r1, #0x1800  41 f4 c0 55  →  41 f4 80 55
# NB: ikke isoleret om patchen stadig er nødvendig efter cma=128M-fixet — beholdt
# fordi den er målt uskadelig og var en del af den verificerede kæde.
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "/opt/hybris/libhybris-hwcomposerwindow.so.1.0.0"
data = open(path, "rb").read()
MOV_OLD = bytes([0x4F, 0xF4, 0xC0, 0x52])
MOV_NEW = bytes([0x4F, 0xF4, 0x80, 0x52])
ORR_OLD = bytes([0x41, 0xF4, 0xC0, 0x55])
ORR_NEW = bytes([0x41, 0xF4, 0x80, 0x55])

a = data.replace(MOV_OLD, MOV_NEW)
b = a.replace(ORR_OLD, ORR_NEW)
if a == data and b == data:
    print(f"{path}: ingen af mønstrene fundet — allerede patchet eller forkert fil")
    sys.exit(1)
if not path.endswith(".orig"):
    open(path + ".orig", "wb").write(data)
open(path, "wb").write(b)
print(f"{path}: mov {data.count(MOV_OLD)}->{b.count(MOV_NEW)}, orr {data.count(ORR_OLD)}->{b.count(ORR_NEW)} — backup: {path}.orig")
