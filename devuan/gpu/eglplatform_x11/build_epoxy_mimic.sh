#!/bin/bash
# build_epoxy_mimic.sh — bygger og kører epoxy-mimic'en på boksen.
#
# Formål: teste libepoxy's EGL-dispatch (mønster A: eglCreateContext 0x300c
# uden at ramme wrapperen). Se FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md §9.1.
#
# Kør fra laptoppen:  bash devuan/gpu/eglplatform_x11/build_epoxy_mimic.sh
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/epoxy_mimic.c

scp -i "$KEY" "$SRC" "$BOX:/root/epoxy_mimic.c"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'set -x
gcc -O0 -g -o /root/epoxy_mimic /root/epoxy_mimic.c \
    -I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm \
    && LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
       EGL_PLATFORM=x11 DISPLAY=:0 /root/epoxy_mimic 2>&1 \
       | tee /root/epoxy_mimic.txt
echo "exit=$?"
tail -5 /root/epoxy_mimic.txt'
