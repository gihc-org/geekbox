#!/bin/bash
set -uo pipefail
BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/min_es_create.c
scp -i "$KEY" "$SRC" "$BOX:/root/min_es_create.c"
ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'gcc -O0 -o /root/min_es_create \
    /root/min_es_create.c -I/usr/local/include -L/opt/hybris \
    -lEGL -lhybris-common -ldl -lrt -lm && \
    LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
    EGL_PLATFORM=x11 DISPLAY=:0 /root/min_es_create 2>&1 | grep -av "system-shim\|libPVR\|libRL"; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
