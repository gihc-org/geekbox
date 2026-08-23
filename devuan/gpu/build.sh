#!/bin/bash
# build.sh — cross-byg GPU-binærerne til armhf på laptoppen (M0, aug 2026).
#
# Baggrund: boks 1 er ikke længere det eneste byggehost — alt bygges her med
# g++-arm-linux-gnueabihf mod vendor_root-libs og distribueres til boksene.
# Link-kontrakten er verificeret: NEEDED-listen på den cross-byggede test_triangle
# matcher den oprindelige boks-byggede 1:1 (readelf -d), og den kørte med identisk
# GL_VERSION/GL_RENDERER + 500 frames på boks 1 (23. aug 2026).
#
# Brug:  devuan/gpu/build.sh [output-dir]   (default: devuan/gpu/bin/)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
INC="$REPO/vendor_root/usr/local/include"
LIB="$REPO/vendor_root/usr/local/lib"
OUT="${1:-$HERE/bin}"
mkdir -p "$OUT"

# To header-fælder (fundet i M0):
#  - hybris' hwcomposer_window.h inkluderer <hardware/...> (Android-konvention)
#    → -I .../include/android
#  - ... og "nativewindowbase.h" med bare navn
#    → -I .../include/hybris/eglplatformcommon
INCLUDES=(-I "$INC" -I "$INC/android" -I "$INC/hybris/eglplatformcommon")
LIBS=(-L "$LIB" -Wl,-rpath-link,"$LIB")

echo "== test_triangle =="
arm-linux-gnueabihf-g++ -O2 -o "$OUT/test_triangle" "$HERE/test_triangle.cpp" \
    "${INCLUDES[@]}" "${LIBS[@]}" \
    -lhybris-hwcomposerwindow -lEGL -lGLESv2 -lhardware -lm

echo "== system_shim.so =="
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -o "$OUT/system_shim.so" "$HERE/system_shim.c"

echo
echo "FÆRDIG → $OUT"
file "$OUT/test_triangle" "$OUT/system_shim.so"
