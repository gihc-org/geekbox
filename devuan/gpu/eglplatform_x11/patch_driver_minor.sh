#!/bin/bash
# patch_driver_minor.sh — lapper driveren (libIMGegl.so) til at tillade
# EGL_CONTEXT_MINOR_VERSION=2 (ES 3.2-forespørgsel) for major=3-kontekster.
#
# Baggrund (målt 24. aug 2026): Firefox' ES-attempt beder om
# {PROFILE_MASK, MAJOR 3, MINOR 2, ...}. Driverens IMGeglCreateContext
# kræver minor<=1 for major=3 (0x9190-0x9194: ldr r3,[sp,#16]; cmp r3,#1;
# bhi.w 95f0 → EGL_BAD_MATCH 0x3009) — driveren understøtter kun ES 3.1.
# Lap: 0x9194: f2 00 82 2c (bhi.w 95f0) → 00 bf 00 bf (to NOP'er).
#
# /system er read-only → kopi i /root + bind-mount (varer til genstart).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=/system/vendor/lib/libIMGegl.so
DST=/root/egl_patch/libIMGegl.so
OFF=0x9194
OFFDEC=37268

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" "
set -e
mkdir -p /root/egl_patch
if [ ! -f /root/libIMGegl.orig ]; then
  cp -a $SRC /root/libIMGegl.orig
  echo 'backup: /root/libIMGegl.orig'
fi
cp -a /root/libIMGegl.orig $DST
echo '== før =='
od -A x -t x1 -j $OFFDEC -N 8 /root/libIMGegl.orig
printf '\x00\xbf\x00\xbf' | dd of=$DST bs=1 seek=$OFFDEC count=4 conv=notrunc
echo '== efter =='
od -A x -t x1 -j $OFFDEC -N 8 $DST
md5sum $SRC $DST /root/libIMGegl.orig
mountpoint -q $SRC || mount --bind $DST $SRC
mount | grep 'libIMGegl.so'
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > \"\$f\" 2>/dev/null; done
"
