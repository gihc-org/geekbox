#!/bin/bash
# testflash.sh — bygger og flasher et image til at teste overscan-fixet.
#
# Bygger UDEN DTB-ændring (DTB_PATCH=none), så boot-kæden er byte-identisk med den
# der virker. Det er kun rootfs'en der er ny: fb_overscan.py + autostart-linjen,
# tapet-symlinket og den aktuelle myinit.sh.
#
# Boksen skal være i loader-tilstand NÅR FLASHNINGEN STARTER — scriptet siger til
# og venter på ENTER inden da:
#   strøm fra boksen → USB-kabel i boksens OTG-port og i laptoppen →
#   hold update-knappen nede → sæt strøm til → slip knappen efter et par sekunder.
#
# Kør:  sudo devuan/testflash.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
IMG=$PROJ/devuan/update_devuan.img
UPGRADE=$PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool

[ "$(id -u)" = 0 ] || { echo "FEJL: kør med sudo — 'sudo devuan/testflash.sh'"; exit 1; }

echo "== 1/3: bygger imaget (DTB_PATCH=none) =="
DTB_PATCH=none "$PROJ/devuan/09_make_emmc_img.sh"

echo
echo "== 2/3: kontrol af imaget =="
python3 "$PROJ/devuan/patch_uboot_logo.py" "$IMG" --prop rockchip,uboot-logo-on --check
python3 "$PROJ/devuan/patch_uboot_logo.py" "$IMG" --prop rockchip,disp-policy --check
echo "   (begge skal stå på vendor-værdierne: uboot-logo-on=1, disp-policy=2)"

echo
echo "== 3/3: flashning =="
echo "Sæt boksen i loader-tilstand NU:"
echo "  strøm fra → USB i OTG-porten → hold update-knappen → strøm på → slip knappen"
echo "(Update-knappen giver loader-tilstand, ikke ægte Mask ROM — det er nok til UF.)"
echo
# Rockchip-enheder melder sig med vendor-id 2207. Vi tjekker FØR flashningen, så en
# manglende loader-tilstand ikke først opdages når værktøjet hænger på "Loading firmware".
while true; do
    read -r -p "Tryk ENTER når boksen er i loader-tilstand (Ctrl+C for at afbryde) " _
    if lsusb 2>/dev/null | grep -qi "2207:"; then
        echo "   Rockchip-enhed fundet på USB:"
        lsusb | grep -i "2207:" | sed "s/^/     /"
        break
    fi
    echo "   INGEN Rockchip-enhed på USB (leder efter vendor-id 2207)."
    echo "   Tjek kablet — det skal i boksens OTG-port — og gentag knap-proceduren."
done
echo
if "$UPGRADE" uf "$IMG"; then
    echo
    echo "== FÆRDIG =="
    echo "Tag strømmen af og på. FØRSTE boot skal vise:"
    echo "  * blåt LXDE-tapet"
    echo "  * et synligt panel i bunden med menu-ikon til venstre og ur til højre"
    echo
    echo "Bagefter, når du kender boksens IP:"
    echo "  ssh -i ~/.ssh/geekbox_key root@<ip> 'bash -s' < devuan/emmc_first_boot.sh"
else
    echo
    echo "FEJL: flashningen gik ikke igennem — er boksen i loader-tilstand?"
    echo "Nødplan, altid brugbar:"
    echo "  sudo $UPGRADE uf $PROJ/Geekbox_Lubuntu_V160309/Geekbox_Lubuntu_V160309/update.img"
    exit 1
fi
