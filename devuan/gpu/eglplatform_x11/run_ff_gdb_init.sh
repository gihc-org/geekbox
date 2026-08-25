#!/bin/bash
# Trin 3: kører gdb_ff_init.cmd på boksen (returværdier + MOZ_LOG).
# Output: /root/ff_init_trace.log (+ nøglelinjer i terminalen).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
CMD=devuan/gpu/eglplatform_x11/gdb_ff_init.cmd

scp -i "$KEY" "$CMD" "$BOX:/root/ff_init_trace.cmd"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
cd /root && timeout 210 gdb -q -batch -x /root/ff_init_trace.cmd \
  --args /usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
  file:///root/webgl_test.html > /root/ff_init_trace.log 2>&1; \
echo "== nøglelinjer =="; grep -aE "^G-ret|^A2!-ret|^V!-ret|^H-ret|^R-ret|^P-ret|^H |Failed to create EGLContext|Failed to load symbols|Empty GL version|GL version detected|GLContext::InitWithPrefix|FEATURE" /root/ff_init_trace.log | head -120; \
echo "== antal =="; for p in "^G-ret" "^A2!-ret" "^V!-ret" "^H-ret" "^R-ret" "^P-ret"; do printf "%s %s\n" "$p" "$(grep -acE "$p" /root/ff_init_trace.log)"; done; \
pkill -9 -x firefox-esr 2>/dev/null; \
chvt 7 2>/dev/null; \
for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
echo "== vt/hdmi =="; cat /sys/class/tty/tty0/active; cat /sys/class/display/HDMI/enable'
