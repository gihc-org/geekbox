#!/bin/bash
# Trin 1: kør Firefox under gdb til første eglBindAPI og dump mappings.
# Output: /root/ff_gdb_map.log — find linjen med /system/lib/libEGL.so og
# notér start-adressen (første kolonne, f.eks. f6c14000).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
CMD=devuan/gpu/eglplatform_x11/gdb_ff_bindapi_map.cmd

scp -i "$KEY" "$CMD" "$BOX:/root/ff_gdb_map.cmd"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
cd /root && gdb -q -batch -x /root/ff_gdb_map.cmd \
  --args /usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
  file:///root/webgl_test.html 2>&1 | tee /root/ff_gdb_map.log; \
echo "== libEGL-baser =="; grep -E "/system/lib/libEGL.so|/opt/hybris/libEGL" /root/ff_gdb_map.log; \
echo "== vt/hdmi =="; cat /sys/class/tty/tty0/active; cat /sys/class/display/HDMI/enable'
