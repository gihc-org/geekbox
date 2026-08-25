#!/bin/bash
# patch_bindapi.sh — lapper wrapperens eglBindAPI til at acceptere ES-API'er.
#
# Baggrund (målt 24. aug 2026): Firefox' ES-vej kalder
# eglBindAPI(EGL_OPENGL_ES_API/ES2/ES3 = 0x30A1/0x30A2/0x30A3), men
# wrapperen /opt/hybris/libEGL.so.1 delegere uredigeret til Android-loaderens
# eglBindAPI (+0x11dc0 i /system/lib/libEGL.so) → driverens eglBindAPI
# (libEGL_POWERVR_ROGUE.so), som kun accepterer 0x30A0 (GL) og ellers svarer
# EGL_BAD_PARAMETER (0x300C). Firefox' ES-attempt dør derfor FØR
# eglCreateContext ("Failed to create EGLContext!: 0x300c") → SW-WR-fallback.
#
# Lap: ersätt eglBindAPI's prolog (VMA/file-offset 0x36c8) med
#   movs r0, #1      (2001)
#   pop  {r7, pc}    (bd80)
# dvs. altid returnere EGL_TRUE uden at kalde Android/driveren. Målt:
# Android's eglCreateContext tjekker IKKE currentApi — ES3-create virker
# allerede efter en fejlet bind (es3_config_probe), så no-op'en er sikker.
#
# Gen-anvend: hvis /opt/hybris geninstalleres, kør scriptet igen.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
EGL=/opt/hybris/libEGL.so.1
OFF=0x36c8
OFFDEC=14024

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" "
set -e
echo '== før =='
od -A x -t x1 -j $((OFF)) -N 8 $EGL
ls -la /opt/hybris/libEGL.so*
if [ ! -f /root/libEGL.so.1.orig ]; then
  cp -a $EGL /root/libEGL.so.1.orig
  echo 'backup: /root/libEGL.so.1.orig'
fi
md5sum $EGL
printf '\x01\x20\x80\xbd' | dd of=$EGL bs=1 seek=$OFFDEC count=4 conv=notrunc
echo '== efter =='
od -A x -t x1 -j $((OFF)) -N 8 $EGL
md5sum $EGL
echo '== verificér med bindapi_probe =='
gcc -O0 -o /root/bindapi_probe /root/bindapi_probe.c -I/usr/local/include \
    -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm 2>/dev/null || true
LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
    EGL_PLATFORM=x11 DISPLAY=:0 /root/bindapi_probe 2>&1 | grep eglBindAPI
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > \"\$f\" 2>/dev/null; done
"
