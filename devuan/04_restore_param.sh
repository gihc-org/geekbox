#!/bin/bash
# 04: Gendanner den originale parameter på eMMC (root=LABEL=linuxroot) — tilbage til Lubuntu-boot.
# Forudsætning: boksen er i MASK ROM-tilstand og forbundet via USB OTG.
# Kør: sudo devuan/04_restore_param.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
UT="$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool"

lsusb | grep -q 2207:330a || { echo "FEJL: boksen ses ikke i Mask ROM (2207:330a)"; exit 1; }

"$UT" db "$PROJ/extracted/Loader.bin"
"$UT" wl 0 "$PROJ/extracted/parameter"
"$UT" rd
echo "== FÆRDIG. Original parameter gendannet — boksen booter Lubuntu fra eMMC. =="
