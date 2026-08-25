#!/bin/bash
# Bygger og kører desktop_create_probe på boksen.
set -uo pipefail
BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/desktop_create_probe.c
scp -i "$KEY" "$SRC" "$BOX:/root/desktop_create_probe.c"
ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'gcc -O0 -o /root/desktop_create_probe \
    /root/desktop_create_probe.c -I/usr/local/include -L/opt/hybris \
    -lEGL -lhybris-common -ldl -lrt -lm && \
    LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
    EGL_PLATFORM=x11 DISPLAY=:0 /root/desktop_create_probe 2>&1 | \
    grep -aE "cfg_|api=|DONE"; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
