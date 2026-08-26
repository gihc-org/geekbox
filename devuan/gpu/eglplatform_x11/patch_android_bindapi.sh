#!/bin/bash
# patch_android_bindapi.sh — lapper Android-loaderens eglBindAPI via bind-mount.
#
# Baggrund (målt 24. aug 2026): wrapperens eglBindAPI delegere til
# Android-loaderens eglBindAPI (/system/lib/libEGL.so +0x11dc0) → driverens
# eglBindAPI (libEGL_POWERVR_ROGUE.so), som kun accepterer EGL_OPENGL_API
# (0x30A0) og ellers svarer EGL_BAD_PARAMETER (0x300C). Firefox' ES-vej
# (eglBindAPI 0x30A1/0x30A2) dør derfor FØR eglCreateContext →
# "Failed to create EGLContext!: 0x300c" → SW-WR-fallback.
#
# Lap: i Android-loaderens eglBindAPI (+0x11df8) erstattes
#   mov r0, r6   (4630)      <- api
#   blx r1       (4788)      <- kald driverens bindAPI
# med
#   movw r0, #0x30A0 (43 f2 a0 00)  <- normalisér til OPENGL_API (GL)
#   blx r1          (88 47)          <- kald driverens bindAPI(GL)
#   pop {r4-r6,pc} (70 bd)
# så ES-bind (0x30A1/0x30A2/0x30A3) returnerer TRUE OG sætter currentApi=GL
# hos driveren (nødvendigt for driverens create; målt: no-op gav currentApi=0
# og driver-create returnerede NULL uden fejl).
#
# Anden lap (chooseConfig): Firefox' ES-attempts forespørgsel har INGEN
# EGL_RENDERABLE_TYPE-entry (0x3038 i listen er EGL_NONE-terminator), så
# Android's eglChooseConfig tager default-stien og driveren returnerer
# GL-configs (0x2...) — ES3-create fejler BAD_MATCH. Lap:
#   a) tving no-RT-forespørgsler ind på ES2-stien (0x112bc: d0 3f → 06 e0)
#   b) tving RT-entry med ikke-ES2-værdi ind på ES2-stien (0x112c2: d1 3c → 03 e0)
#   c) ES2-stiens prepended-par skiftes fra {EGL_SAMPLE_BUFFERS=0x3032,1,
#      EGL_SAMPLES=0x3031,4} til {EGL_RENDERABLE_TYPE=0x3040, ES2|ES3=0x44,
#      EGL_NONE=0x3038,0}, så listen faktisk kræver ES-capable configs.
#   d) attrib-parseren kører KUN når debug.egl.force_msaa=true, og loopets
#      tæller/caveat-pointer frøs fra strcmp-resultatet (skal være 0) —
#      (0x11288: cmp r0,#0 / 0x1128a: bne 1133e) erstattes med
#      "movs r0,#0; nop" (28 00 d1 58 → 00 20 00 bf), så parseren altid kører
#      MED r0=0 (tæller/caveat korrekt).
#   e) ES2-stiens caveat-tjek (0x112c4: cbz r0,112cc) læser et leftover-literal
#      i r0 (kollaps, når forespørgslen ikke har EGL_CONFIG_CAVEAT) — gør
#      betingelsen ubetinget (b1 10 → 02 e0, dvs. b.n 112cc).
# Resultat: ES-attempt får config 0x12 (ES2|ES3) hvor ES3-create virker
# (målt med es3_config_probe).
#
# /system er et read-only loop-image → vi patcher en kopi i /root og
# bind-monter den over /system/lib/libEGL.so (varer til genstart; kør
# scriptet igen efter reboot).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=/system/lib/libEGL.so
DST=/root/egl_patch/libEGL.so
OFF=0x11df8
OFFDEC=73208
CC_OFF=0x112c2
CC_DEC=70338
CC2_OFF=0x112bc
CC2_DEC=70332
CC3_OFF=0x112ce
CC3_DEC=70350
CC4_OFF=0x112d6
CC4_DEC=70358
CC5_OFF=0x112d8
CC5_DEC=70360
CC6_OFF=0x1128a
CC6_DEC=70282
CC6B_OFF=0x11288
CC6B_DEC=70280
CC7_OFF=0x112c4
CC7_DEC=70340

ssh -i "$KEY" -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$BOX" "
set -e
mkdir -p /root/egl_patch
if [ ! -f /root/libEGL_android.orig ]; then
  cp -a $SRC /root/libEGL_android.orig
  echo 'backup: /root/libEGL_android.orig'
fi
cp -a /root/libEGL_android.orig $DST
echo '== før (original) =='
od -A x -t x1 -j $OFFDEC -N 8 /root/libEGL_android.orig
echo '== lapper kopi =='
printf '\x43\xf2\xa0\x00\x88\x47\x70\xbd' | dd of=$DST bs=1 seek=$OFFDEC count=8 conv=notrunc
od -A x -t x1 -j $OFFDEC -N 8 $DST
echo '== lapper chooseConfig (RT=0 -> ES2-sti) =='
od -A x -t x1 -j $CC_DEC -N 4 $DST
printf '\x03\xe0' | dd of=$DST bs=1 seek=$CC_DEC count=2 conv=notrunc
od -A x -t x1 -j $CC_DEC -N 4 $DST
printf '\x06\xe0' | dd of=$DST bs=1 seek=$CC2_DEC count=2 conv=notrunc
od -A x -t x1 -j $CC2_DEC -N 4 $DST
echo '== lapper ES2-stiens prepended-attributter (RT=ES2|ES3, EGL_NONE) =='
printf '\x43\xf2\x40\x02' | dd of=$DST bs=1 seek=$CC3_DEC count=4 conv=notrunc
printf '\x44\x20' | dd of=$DST bs=1 seek=$CC4_DEC count=2 conv=notrunc
printf '\x43\xf2\x38\x03' | dd of=$DST bs=1 seek=$CC5_DEC count=4 conv=notrunc
od -A x -t x1 -j $CC3_DEC -N 16 $DST
echo '== lapper force_msaa-tjek (altid kør parseren) =='
printf '\x00\x20\x00\xbf' | dd of=$DST bs=1 seek=$CC6B_DEC count=4 conv=notrunc
od -A x -t x1 -j $CC6B_DEC -N 6 $DST
printf '\x02\xe0' | dd of=$DST bs=1 seek=$CC7_DEC count=2 conv=notrunc
od -A x -t x1 -j $CC7_DEC -N 4 $DST
echo '== md5 =='
md5sum $SRC $DST /root/libEGL_android.orig
echo '== bind-mount =='
mountpoint -q $SRC || mount --bind $DST $SRC
mount | grep '/system/lib/libEGL.so'
echo '== verificér med bindapi_probe =='
LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
    EGL_PLATFORM=x11 DISPLAY=:0 /root/bindapi_probe 2>&1 | grep -a 'eglBindAPI('
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > \"\$f\" 2>/dev/null; done
"
