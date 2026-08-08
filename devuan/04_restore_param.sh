#!/bin/bash
# 04: Gendanner den originale parameter på eMMC (root=LABEL=linuxroot) — tilbage til Lubuntu-boot.
# Forudsætning: boksen er i loader-tilstand (Update + Reboot) og forbundet via USB OTG.
# Kør: sudo devuan/04_restore_param.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
UT="$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool"

lsusb | grep -q 2207: || { echo "FEJL: ingen Rockchip-enhed (2207:xxxx) på USB"; exit 1; }

echo "== skriver original parameter tilbage =="
"$UT" DI -p "$PROJ/devuan/parameter_orig.txt"
echo "== FÆRDIG. Genstart boksen — den booter Lubuntu fra eMMC igen. =="
