#!/bin/bash
# Bygger og kører firefox_seq_probe på boksen (Firefox-sekvens-replika).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/firefox_seq_probe.c

scp -i "$KEY" "$SRC" "$BOX:/root/firefox_seq_probe.c"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'gcc -O0 -g -o /root/firefox_seq_probe \
    /root/firefox_seq_probe.c -I/usr/local/include -L/opt/hybris \
    -lEGL -lhybris-common -ldl -lrt -lm && echo BUILD-OK && \
    for v in "base" "preamble" "nodefault" "preamble_nodefault" "shim" "shim_preamble"; do \
      case $v in \
        base)                E=""; PRE="/root/system_shim.so";; \
        preamble)            E="PREAMBLE=1"; PRE="/root/system_shim.so";; \
        nodefault)           E="XDISPLAY=0"; PRE="/root/system_shim.so";; \
        preamble_nodefault)  E="PREAMBLE=1 XDISPLAY=0"; PRE="/root/system_shim.so";; \
        shim)                E=""; PRE="/root/system_shim.so /root/egl_platform_shim.so";; \
        shim_preamble)       E="PREAMBLE=1"; PRE="/root/system_shim.so /root/egl_platform_shim.so";; \
      esac; \
      env $E LD_PRELOAD="$PRE" LD_LIBRARY_PATH=/opt/hybris \
          EGL_PLATFORM=x11 DISPLAY=:0 /root/firefox_seq_probe 2>&1 | \
          grep -av "system-shim\|libPVR\|libRL" > /root/firefox_seq_$v.txt; \
      echo "== $v: create-linjer (cfg 0x12 = config 4) =="; \
      sed -n "/--- config 4 ---/,/--- config 5 ---/p" /root/firefox_seq_$v.txt; \
    done; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
