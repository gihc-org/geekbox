#!/bin/bash
# 03: Skriver den modificerede parameter (root=LABEL=sdrootfs1) til eMMC sector 0.
# Forudsætning: boksen er i loader- eller Mask ROM-tilstand og forbundet via USB OTG.
# Kør: sudo devuan/03_boot_sdroot.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
UT="$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool"
BAK="$PROJ/devuan/param_backup.bin"

lsusb | grep -q 2207: || { echo "FEJL: ingen Rockchip-enhed (2207:xxxx) på USB"; exit 1; }
rm -f "$BAK"

echo "== backup af eMMC sector 0-7 -> devuan/param_backup.bin =="
"$UT" RL 0 8 "$BAK" || true
if [ ! -f "$BAK" ]; then
    echo "== direkte læsning fejlede — uploader loader først (Mask ROM-tilstand) =="
    "$UT" DB "$PROJ/extracted/Loader.bin"
    "$UT" RL 0 8 "$BAK"
fi
head -c4 "$BAK" | grep -q PARM \
    || { echo "FEJL: sector 0 indeholder ikke PARM-magic — afbryder uden at ændre noget"; exit 1; }

echo "== skriver ny parameter (root=LABEL=sdrootfs1) =="
"$UT" WL 0 "$PROJ/devuan/parameter_sdroot"

echo "== verificerer skrivningen =="
VER="$PROJ/devuan/param_verify.bin"; rm -f "$VER"
"$UT" RL 0 8 "$VER"
head -c 580 "$VER" | cmp -s - "$PROJ/devuan/parameter_sdroot" \
    && echo "VERIFICERET: parameter på eMMC matcher" \
    || { echo "ADVARSEL: verificering fejlede — boksen kan være uændret"; exit 1; }
rm -f "$VER"

echo "== genstarter boksen =="
"$UT" RD || true
echo "== FÆRDIG. Boksen booter nu Devuan fra SD-kortet (hvis det sidder korrekt i). =="
echo "   Gendan Lubuntu: sudo devuan/04_restore_param.sh"
