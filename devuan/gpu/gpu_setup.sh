#!/bin/bash
# gpu_setup.sh — gør en NYFLASHET boks GPU-klar. KØRES PÅ LAPTOPPEN.
#
# Brug:   devuan/gpu/gpu_setup.sh <boks-ip> [kilde-boks-ip]
# Kilde-boksen (default boks 1, 192.168.0.188) leverer de færdigkompilerede
# binærer (test_triangle, system_shim.so, gpu_up.sh) — så den nye boks IKKE
# behøver gcc/gdb/strace.
#
# Forudsætninger: boksen er flashet med det nuværende image (09/testflash.sh) —
# parameteren har cma=128M, og myinit monterer /system automatisk når system.img
# findes. Imaget rummer IKKE selve GPU-stakken (rootfs 1,4 GB, 84 % fuld med
# firefox — system.img alene er ~200 MB); den lægges på her i stedet.
#
# Bagefter: STRØM-CYKL boksen (se DOK §5.15 — HDMI vågner kun på den måde efter
# GPU-sessioner, og myinit skal køre for at montere /system).
set -euo pipefail
BOX=${1:?brug: gpu_setup.sh <boks-ip> [kilde-boks-ip]}
SRC=${2:-192.168.0.188}
KEY=~/.ssh/geekbox_key
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VENDOR="$REPO/vendor_root"
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "== 1. vendors system.img (~200 MB) til /usr/local/share/libhybris =="
ssh -i "$KEY" "root@$BOX" 'mkdir -p /usr/local/share/libhybris'
scp -i "$KEY" "$VENDOR/usr/local/share/libhybris/system.img" "root@$BOX:/usr/local/share/libhybris/"

echo "== 2. hybris-broer til /opt/hybris (fra vendor_root, usage-patchet) =="
STAGE=$(mktemp -d)
cp -a "$VENDOR/usr/local/lib"/libhybris-common.so* \
      "$VENDOR/usr/local/lib"/libEGL.so* \
      "$VENDOR/usr/local/lib"/libGLESv1_CM.so* \
      "$VENDOR/usr/local/lib"/libGLESv2.so* \
      "$VENDOR/usr/local/lib"/libhybris-hwcomposerwindow.so* \
      "$VENDOR/usr/local/lib"/libhybris-eglplatformcommon.so* \
      "$VENDOR/usr/local/lib"/libandroid-properties.so* \
      "$VENDOR/usr/local/lib"/libhardware.so* \
      "$VENDOR/usr/local/lib"/libsync.so* \
      "$STAGE/"
cp -a "$VENDOR/usr/local/lib/libhybris" "$STAGE/"
python3 "$HERE/patch_hwc_usage.py" "$STAGE/libhybris-hwcomposerwindow.so.1.0.0"
ssh -i "$KEY" "root@$BOX" 'mkdir -p /opt/hybris /usr/local/lib/libhybris'
scp -i "$KEY" -r "$STAGE"/* "root@$BOX:/opt/hybris/"
ssh -i "$KEY" "root@$BOX" 'cp /opt/hybris/libhybris/*.so /usr/local/lib/libhybris/'
rm -rf "$STAGE"

echo "== 3. headere (kun nødvendige hvis der skal BYGGES på boksen) =="
TAR=$(mktemp)
(cd "$VENDOR/usr/local/include" && tar czf "$TAR" .)
ssh -i "$KEY" "root@$BOX" 'mkdir -p /usr/local/include'
ssh -i "$KEY" "root@$BOX" 'tar xzf - -C /usr/local/include' < "$TAR"
rm -f "$TAR"

echo "== 4. færdige binærer fra kilde-boksen =="
for f in test_triangle system_shim.so gpu_up.sh; do
    scp -i "$KEY" "root@$SRC:/root/$f" "root@$BOX:/root/"
done

echo "== 5. sikr at myinit monterer /system (idempotent — ældre images) =="
scp -i "$KEY" "$HERE/diagnostik/patch_myinit.py" "root@$BOX:/root/"
ssh -i "$KEY" "root@$BOX" 'python3 /root/patch_myinit.py'

echo
echo "FÆRDIG. Strøm-cykl nu boksen — ved næste boot monterer myinit /system selv."
echo "Bagefter (X skal stoppes under GPU-brug):"
echo "  ssh -i $KEY root@$BOX 'sh /root/gpu_up.sh'"
echo "  ssh -i $KEY root@$BOX 'service nodm stop'"
echo "  ssh -i $KEY root@$BOX 'LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=hwcomposer /root/test_triangle'"
echo "Husk: hwc og X kan ikke deles om skærmen — HDMI kræver strøm-cyklus bagefter (DOK §5.15)."
