#!/bin/bash
# 03: Skriver den modificerede parameter (root=LABEL=sdrootfs1) til eMMC sector 0.
# Forudsætning: boksen er i MASK ROM-tilstand og forbundet via USB OTG.
# Kør: sudo devuan/03_boot_sdroot.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
UT="$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool"

lsusb | grep -q 2207:330a || { echo "FEJL: boksen ses ikke i Mask ROM (2207:330a)"; exit 1; }

echo "== uploader loader til boksen (maskrom -> loader-tilstand) =="
"$UT" db "$PROJ/extracted/Loader.bin"

echo "== backup af eMMC sector 0-7 -> devuan/param_backup.bin =="
"$UT" rl 0 8 "$PROJ/devuan/param_backup.bin"
head -c4 "$PROJ/devuan/param_backup.bin" | grep -q PARM \
    || { echo "FEJL: sector 0 indeholder ikke PARM-magic — afbryder uden at ændre noget"; exit 1; }

echo "== skriver ny parameter (root=LABEL=sdrootfs1) =="
"$UT" wl 0 "$PROJ/devuan/parameter_sdroot"

echo "== genstarter boksen =="
"$UT" rd
echo "== FÆRDIG. Boksen booter nu Devuan fra SD-kortet (hvis det sidder i). =="
echo "   Gendan Lubuntu: sudo devuan/04_restore_param.sh"
