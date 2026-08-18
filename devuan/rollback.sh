#!/bin/bash
# rollback.sh — sætter DTB-flaget rockchip,uboot-logo-on tilbage til 1 i
# devuan/update_devuan.img og flasher boksen med det.
#
# Baggrund: uboot-logo-on = 0 gør at boksen IKKE booter (LED lilla, aldrig blå) —
# flaget styrer også U-Boots egen display-init. Se DEBUG-SORT-SKAERM.md.
#
# Boksen skal være i loader-tilstand FØR scriptet køres:
#   strøm fra boksen → USB-kabel i boksens OTG-port og i laptoppen →
#   hold update-knappen nede → sæt strøm til → slip knappen efter et par sekunder.
#
# Kør:  sudo devuan/rollback.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
IMG=$PROJ/devuan/update_devuan.img
PATCHER=$PROJ/devuan/patch_uboot_logo.py
UPGRADE=$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool

[ "$(id -u)" = 0 ] || { echo "FEJL: kør med sudo — 'sudo devuan/rollback.sh'"; exit 1; }
[ -f "$IMG" ]     || { echo "FEJL: $IMG findes ikke — kør 09 først"; exit 1; }
[ -x "$UPGRADE" ] || { echo "FEJL: $UPGRADE findes ikke"; exit 1; }

echo "== 1/3: sætter uboot-logo-on tilbage til 1 i imaget =="
python3 "$PATCHER" "$IMG" --prop rockchip,uboot-logo-on --value 1

echo
echo "== 2/3: kontrol — begge DTB-kopier skal stå på 1 =="
python3 "$PATCHER" "$IMG" --prop rockchip,uboot-logo-on --check
# stop hvis en kopi stadig står på 0
if python3 "$PATCHER" "$IMG" --prop rockchip,uboot-logo-on --check | grep -q "= 0$"; then
    echo "FEJL: en DTB-kopi står stadig på 0 — flasher ikke"
    exit 1
fi

echo
echo "== 3/3: flasher boksen (5-10 minutter) =="
echo "   Hænger den på 'Loading firmware...' er boksen ikke i loader-tilstand:"
echo "   strøm fra, USB i OTG-porten, hold update-knappen, strøm på, slip knappen."
echo
if "$UPGRADE" uf "$IMG"; then
    echo
    echo "== FÆRDIG =="
    echo "Tag strømmen af og på. Dioderne skal gå lilla -> blå, og boksen booter"
    echo "med blå skrivebordsbaggrund og panel i bunden."
else
    echo
    echo "FEJL: flashningen gik ikke igennem."
    echo "Tjek at boksen er i loader-tilstand (se ovenfor) og kør scriptet igen."
    echo "Nødplan, altid brugbar: sudo $UPGRADE uf \\"
    echo "  $PROJ/Geekbox_Lubuntu_V160309/Geekbox_Lubuntu_V160309/update.img"
    exit 1
fi
