#!/bin/bash
# 04: Gendanner den originale parameter på eMMC (root=LABEL=linuxroot) — tilbage til Lubuntu-boot.
# Forudsætning: boksen er i loader- eller Mask ROM-tilstand og forbundet via USB OTG.
# Kør: sudo devuan/04_restore_param.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
UT="$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool"

lsusb | grep -q 2207: || { echo "FEJL: ingen Rockchip-enhed (2207:xxxx) på USB"; exit 1; }

echo "== skriver original parameter tilbage =="
"$UT" WL 0 "$PROJ/extracted/parameter" || {
    "$UT" DB "$PROJ/extracted/Loader.bin"
    "$UT" WL 0 "$PROJ/extracted/parameter"
}
"$UT" RD || true
echo "== FÆRDIG. Original parameter gendannet — boksen booter Lubuntu fra eMMC. =="
