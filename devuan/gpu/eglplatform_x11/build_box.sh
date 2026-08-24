#!/bin/sh
# build_box.sh — byg eglplatform_x11-prototypen PÅ BOKSEN (fallback-host,
# fordi armhf-X11-headere mangler på laptoppen; cross-byg kan komme i M4).
#
# Kør på boksen (root):
#   bash devuan/gpu/eglplatform_x11/build_box.sh
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
INC=/usr/local/include
LIB=/opt/hybris          # boksens hybris-biblioteker (README operationskort)
OUT="$HERE/out"
mkdir -p "$OUT"

echo "== eglplatform_x11.so =="
g++ -O2 -fPIC -shared -o "$OUT/eglplatform_x11.so" "$HERE/eglplatform_x11.cpp" \
    -I"$INC" -I"$INC/android" -I"$INC/hybris/eglplatformcommon" \
    -L"$LIB" -Wl,-rpath-link,"$LIB" \
    -lhybris-eglplatformcommon -lhardware -lhybris-common -landroid-properties \
    -lsync -lX11 -ldl -lrt

echo "== test_client_x11 =="
g++ -O2 -o "$OUT/test_client_x11" "$HERE/test_client_x11.cpp" \
    -I"$INC" -I"$INC/android" \
    -L"$LIB" -Wl,-rpath-link,"$LIB" \
    -lEGL -lGLESv2 -lhybris-common -lhardware -lX11 -ldl -lrt -lm

echo "== installér platformen (libEGL leder i /usr/local/lib/libhybris) =="
cp "$OUT/eglplatform_x11.so" /usr/local/lib/libhybris/eglplatform_x11.so
chmod 755 /usr/local/lib/libhybris/eglplatform_x11.so

echo
echo "FÆRDIG → $OUT"
ls -la "$OUT"
