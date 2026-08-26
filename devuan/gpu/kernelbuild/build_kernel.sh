#!/bin/bash
# build_kernel.sh — bygger 3.10.79-kernen (lollipop_kernel, gren geekbox) til GeekBox
# og pakker den som Android-bootimg (ramfs.img) med den ORIGINALE ramdisk + DTB.
#
# Brug:
#   bash devuan/gpu/kernelbuild/build_kernel.sh baseline   # geekbox_defconfig uændret
#   bash devuan/gpu/kernelbuild/build_kernel.sh test       # POWERVR_ROGUE slået fra (1.5-.ko-vej)
#
# Output: devuan/gpu/kernelbuild/out/<mode>/Image + ramfs-<mode>.img (+ config)
#
# Fælder løst her (26. aug 2026, målt):
#  - scripts/gcc-wrapper.py er python2 + streng -Werror-politik → fjernes (kernen
#    bygges med gcc-9 direkte via CROSS_COMPILE).
#  - scripts/dtc's shipped lexer/parser definerer begge YYLTYPE yylloc → lexeren
#    patchet til extern (klassisk bison-3-problem på 3.10).
set -euo pipefail

MODE=${1:?brug: $0 baseline|test}
PROJ=$(cd "$(dirname "$0")/../../.." && pwd)
SRC=/tmp/lollipop_kernel
BUILD=/tmp/kb_${MODE}
OUT="$PROJ/devuan/gpu/kernelbuild/out/$MODE"
KBCC=/tmp/kbcc

[ -x "$KBCC/aarch64-linux-gnu-gcc" ] || { echo "FEJL: mangler $KBCC/aarch64-linux-gnu-gcc"; exit 1; }

echo "== opret frisk byggetræ fra $SRC =="
rm -rf "$BUILD"
mkdir -p "$BUILD"
git -C "$SRC" archive HEAD | tar -x -C "$BUILD"

echo "== workarounds (gcc-wrapper + dtc yylloc) =="
rm -f "$BUILD/scripts/gcc-wrapper.py"
# 3.10 har kun compiler-gcc{3,4,5}.h; gcc-9 leder efter compiler-gcc9.h (via
# gcc_header(__GNUC__)) — kopier 5-serien (attributterne er kompatible).
cp "$BUILD/include/linux/compiler-gcc5.h" "$BUILD/include/linux/compiler-gcc9.h"
# Moderne binutils: .section-flag-syntaksen er "ax" (hverken #alloc/#execinstr
# eller %alloc/%execinstr accepteres af binutils 2.42). Håndter begge gamle former.
sed -i 's/, #alloc, #execinstr/, "ax"/; s/, %alloc, %execinstr/, "ax"/' "$BUILD/arch/arm64/mm/proc.S"
# rtl8188eu (gcc-9): extern __inline i header → out-of-line kopi i flere TU'er =
# multiple definition. Fix: static __inline (pr. TU, ingen konflikt).
sed -i 's/^extern __inline /static __inline /' \
    "$BUILD/drivers/net/wireless/rockchip_wlan/rtl8188eu/include/ieee80211.h"
# Kun .c_shipped findes i friskt træ; bygget kopierer den (og evt. regenererer
# fra .l hvis .l er nyere — derfor touches _shipped til nyere end .l).
sed -i 's/^YYLTYPE yylloc;$/extern YYLTYPE yylloc;/' "$BUILD/scripts/dtc/dtc-lexer.lex.c_shipped"
touch "$BUILD/scripts/dtc/dtc-lexer.lex.c_shipped"
if [ -f "$BUILD/scripts/dtc/dtc-lexer.lex.c" ]; then
    sed -i 's/^YYLTYPE yylloc;$/extern YYLTYPE yylloc;/' "$BUILD/scripts/dtc/dtc-lexer.lex.c"
    touch "$BUILD/scripts/dtc/dtc-lexer.lex.c"
fi

echo "== config ($MODE) =="
cd "$BUILD"
export PATH="$KBCC:$PATH"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- geekbox_defconfig
if [ "$MODE" = test ]; then
    # 1.5-KM-vejen: ingen indbygget PVR-KM; 1.5-.ko'en loades som modul.
    ./scripts/config --disable POWERVR_ROGUE
    # Sikr at modul-støtte er på (1.5-.ko vermagic kræver mod_unload — allerede y i defconfig)
    ./scripts/config --enable MODULES --enable MODULE_UNLOAD
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
fi
cp .config "$OUT.config" 2>/dev/null || { mkdir -p "$OUT"; cp .config "$OUT.config"; }

echo "== byg Image (gcc-9, -j8) =="
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j8 Image 2>&1 | tail -5

mkdir -p "$OUT"
cp arch/arm64/boot/Image "$OUT/Image"
grep -E "CONFIG_(POWERVR_ROGUE|MODULES|MODULE_UNLOAD|LOCALVERSION|IKCONFIG|SMP|PREEMPT)" .config | tee "$OUT/config-notes.txt"
echo "== FÆRDIG: $OUT/Image =="
ls -la "$OUT/Image"
