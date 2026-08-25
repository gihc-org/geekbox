#!/bin/bash
# Trin 2: fuld mønster-A/B-sporing under gdb.
#   - P:  wrapper-eglGetProcAddress (navn + hvem spørger)
#   - G:  wrapper-eglCreateContext (argumenter)
#   - A!: Android-intern eglCreateContext @ 0xdc843534 (base 0xdc83d000 + 0x6534)
#   - R:  eglGetError (fejlen der meldes 0x300c/0x3000)
# Output: /root/ff_gdb_trace.log
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
CMD=devuan/gpu/eglplatform_x11/gdb_ff_trace_mønsterA.cmd

scp -i "$KEY" "$CMD" "$BOX:/root/ff_gdb_trace.cmd"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
cd /root && timeout 150 gdb -q -batch -x /root/ff_gdb_trace.cmd \
  --args /usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
  file:///root/webgl_test.html > /root/ff_gdb_trace.log 2>&1; \
echo "== nøglelinjer =="; grep -aE "^F |^F-ret|^ES-CALL|^A1!|^A2!|^V!|^M!|^RBRK|^H |^R |Failed to create EGLContext|Fallback|FEATURE|installeret|new_objfile" /root/ff_gdb_trace.log | head -220; \
echo "== RBRK-hits (symbol-synlige eglCreateContext) =="; grep -ac "^RBRK!" /root/ff_gdb_trace.log; \
pkill -9 -x firefox-esr 2>/dev/null; \
chvt 7 2>/dev/null; \
for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
echo "== vt/hdmi =="; cat /sys/class/tty/tty0/active; cat /sys/class/display/HDMI/enable'
