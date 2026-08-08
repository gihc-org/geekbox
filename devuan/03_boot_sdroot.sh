#!/bin/bash
# 03: Skriver den modificerede parameter (root=LABEL=sdrootfs1) til eMMC.
# Bruger DI -p (DownloadImage til parameter-partitionen) — virker i loader-tilstand.
# Forudsætning: boksen er i loader-tilstand (Update + Reboot) og forbundet via USB OTG.
# Kør: sudo devuan/03_boot_sdroot.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
UT="$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool"

lsusb | grep -q 2207: || { echo "FEJL: ingen Rockchip-enhed (2207:xxxx) på USB"; exit 1; }

echo "== skriver ny parameter (root=LABEL=sdrootfs1) =="
"$UT" DI -p "$PROJ/devuan/parameter_sdroot"

echo ""
echo "== FÆRDIG hvis der står 'Download image ok'/'success' ovenfor. =="
echo "   Tag strømmen til boksen og sæt den til igen (eller tryk Reboot)."
echo "   Den booter nu Devuan fra SD-kortet, hvis kortet sidder korrekt i."
echo "   Gendan Lubuntu: sudo devuan/04_restore_param.sh"
