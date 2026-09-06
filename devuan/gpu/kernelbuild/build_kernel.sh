#!/bin/bash
# build_kernel.sh — bygger 3.10.79-kernen (lollipop_kernel, gren geekbox) til GeekBox
# og pakker den som Android-bootimg (ramfs.img) med den ORIGINALE ramdisk + DTB.
#
# Brug:
#   bash devuan/gpu/kernelbuild/build_kernel.sh baseline   # geekbox_defconfig uændret
#   bash devuan/gpu/kernelbuild/build_kernel.sh test       # POWERVR_ROGUE slået fra (1.5-.ko-vej)
#   SRC_COMMIT=<hash> bash .../build_kernel.sh baseline    # byg fra et bestemt commit
#   CONFIG_SRC=<fil> bash .../build_kernel.sh baseline     # brug en anden defconfig
#   CONFIG_DISABLE="A B C" bash .../build_kernel.sh baseline  # slå symboler fra
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
SRC=${SRC:-/tmp/lollipop_kernel}
SRC_COMMIT=${SRC_COMMIT:-HEAD}
BUILD=${BUILD_DIR:-/tmp/kb_${MODE}}
OUT="$PROJ/devuan/gpu/kernelbuild/out/$MODE"
KBCC=${KBCC:-/tmp/kbcc}

[ -x "$KBCC/aarch64-linux-gnu-gcc" ] || { echo "FEJL: mangler $KBCC/aarch64-linux-gnu-gcc"; exit 1; }

echo "== opret frisk byggetræ fra $SRC @ $SRC_COMMIT =="
rm -rf "$BUILD"
mkdir -p "$BUILD"
git -C "$SRC" archive "$SRC_COMMIT" | tar -x -C "$BUILD"

echo "== workarounds (gcc-wrapper + dtc yylloc) =="
rm -f "$BUILD/scripts/gcc-wrapper.py"
# 3.10 har kun compiler-gcc{3,4,5}.h; nyere gcc leder efter compiler-gccX.h (via
# gcc_header(__GNUC__)) — kopier 5-serien (attributterne er kompatible).
GCC_MAJOR=$("$KBCC/aarch64-linux-gnu-gcc" -dumpversion | cut -d. -f1)
cp "$BUILD/include/linux/compiler-gcc5.h" "$BUILD/include/linux/compiler-gcc${GCC_MAJOR}.h"
# Moderne binutils: .section-flag-syntaksen er "ax" (hverken #alloc/#execinstr
# eller %alloc/%execinstr accepteres af binutils 2.42). Håndter begge gamle former.
sed -i 's/, #alloc, #execinstr/, "ax"/; s/, %alloc, %execinstr/, "ax"/' "$BUILD/arch/arm64/mm/proc.S"
# rtl8188eu (gcc-9): extern __inline i header → out-of-line kopi i flere TU'er =
# multiple definition. Fix: static __inline (pr. TU, ingen konflikt).
sed -i 's/^extern __inline /static __inline /' \
    "$BUILD/drivers/net/wireless/rockchip_wlan/rtl8188eu/include/ieee80211.h"
# 26. aug 2026: compat clock_gettime64 (403) — nødvendig for bionic 6.0-libc
# (gralloc-lock fejler EINVAL uden den i Firefox' GPU-proces).
# 6. sep 2026: compat-tabellen udvides i stedet til 450 poster; alle nye
# syscalls (404..449) -> sys_ni_syscall (ren ENOSYS). Baggrund: Firefox'
# glean.upload-tråd kalder clone3 (asm-generic nr. 435); 3.10's do_ni_syscall
# dræber processen (SIGILL) i stedet for ENOSYS -> glibc/Rust kan ikke falde
# tilbage på clone -> hele Firefox crasher ~1½-3 min efter start (målt 2×
# 6. sep 2026, cyan-sporet). Med ni-poster får kalderen ENOSYS som normalt.
# 1) udvid compat-tabellen fra 384 til 450 poster
sed -i 's/#define __NR_compat_syscalls\t\t384/#define __NR_compat_syscalls\t\t450/' \
    "$BUILD/arch/arm64/include/asm/unistd.h"
# 2) tilføj poster 384..402 (ni) + 403 (clock_gettime64 -> sys_clock_gettime;
#    timespec64-layoutet matcher native timespec på arm64) + 404..449 (ni)
cat >> "$BUILD/arch/arm64/kernel/sys32.S" <<'EOF'

	.rept 19
	.quad sys_ni_syscall
	.endr
	.quad sys_clock_gettime

	.rept 46
	.quad sys_ni_syscall
	.endr
EOF
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
if [ -n "${CONFIG_SRC:-}" ]; then
    cp "$CONFIG_SRC" "$BUILD/arch/arm64/configs/geekbox_defconfig"
fi
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- geekbox_defconfig
if [ -n "${CONFIG_DISABLE:-}" ]; then
    for c in $CONFIG_DISABLE; do
        echo "   -- disable $c"
        ./scripts/config --disable "$c"
    done
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
fi
if [ "$MODE" = test ]; then
    # 1.5-KM-vejen: ingen indbygget PVR-KM; 1.5-.ko'en loades som modul.
    ./scripts/config --disable POWERVR_ROGUE
    # Sikr at modul-støtte er på (1.5-.ko vermagic kræver mod_unload — allerede y i defconfig)
    ./scripts/config --enable MODULES --enable MODULE_UNLOAD
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
fi
# Ekstra symboler der skal tændes (fx CONFIG_TRACING for 1.5-.ko'ens ftrace-symboler)
if [ -n "${CONFIG_ENABLE:-}" ]; then
    for c in $CONFIG_ENABLE; do
        echo "   -- enable $c"
        ./scripts/config --enable "$c"
    done
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
