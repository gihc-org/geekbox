#!/usr/bin/env python3
"""Patcher SONAME i en kopi af hybris-libbet (samme længde) så proxien kan
linke originalen under et nyt navn uden kollision med "libEGL.so.1" osv."""
import sys

JOBS = [
    ("/opt/hybris/libEGL_r.so", b"libEGL.so.1", b"libEGL_r.so"),
    ("/opt/hybris/libGLESv2_r.so", b"libGLESv2.so.2", b"libGLESv2_r.so"),
]

for path, old, new in JOBS:
    if len(old) != len(new):
        sys.exit(f"FEJL: længde afviger for {path}: {old} != {new}")
    data = open(path, "rb").read()
    if old not in data:
        sys.exit(f"FEJL: {old} ikke fundet i {path}")
    data = data.replace(old, new)
    open(path, "wb").write(data)
    print(f"OK: {path} SONAME {old.decode()} -> {new.decode()}")
