#!/bin/bash
# Bygger og kører es3_config_probe på boksen (ES3/ES2-config-diagnose).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/es3_config_probe.c

scp -i "$KEY" "$SRC" "$BOX:/root/es3_config_probe.c"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'gcc -O0 -g -o /root/es3_config_probe \
    /root/es3_config_probe.c -I/usr/local/include -L/opt/hybris \
    -lEGL -lhybris-common -ldl -lrt -lm && echo BUILD-OK && \
    LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
    EGL_PLATFORM=x11 DISPLAY=:0 /root/es3_config_probe 2>&1 | \
    tee /root/es3_config_probe.txt; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
