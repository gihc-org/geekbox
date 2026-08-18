#!/bin/bash
# extra_packages.sh: Ekstra pakker i rootfs'en — samlet ét sted, så hverken
# 07 (desktop+lyd) eller de øvrige formålsscripts fyldes med løse tilføjelser.
#
# Det eneste der skal rettes er listen nedenfor: tilføj pakkenavne efter behov
# og genkør scriptet (idempotent). Kør derefter 09, så kommer pakkerne med i
# eMMC-imaget. 09's vagt stopper bygningen hvis en forventet pakke mangler.
#
# Kører via qemu-chroot på PC'en — bevidst IKKE på boksen: postinst-scripts der
# bruger systemd-sysusers fejler på 3.10-kernen, men virker fint under PC'ens kernel.
# Kør efter 01 (rootfs bygget), før 09: sudo devuan/extra_packages.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$PROJ/devuan/rootfs

# ── Pakkelisten — tilføj flere pakkenavne her ──────────────────────────
EXTRA_PACKAGES=(
    lxterminal      # terminal-emulator; lxde-core (07) trækker ikke selv en med
)
# ────────────────────────────────────────────────────────────────────────

[ "$(id -u)" = 0 ] || { echo "FEJL: kør med sudo ($ROOTFS er root-ejet)"; exit 1; }
[ -d "$ROOTFS/etc/apt" ] || { echo "FEJL: $ROOTFS ser ufuldstændig ud — kør 01 først"; exit 1; }

cp /usr/bin/qemu-arm-static "$ROOTFS/usr/bin/"
cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"
trap 'rm -f "$ROOTFS/usr/bin/qemu-arm-static"' EXIT

echo "== ekstra pakker: ${EXTRA_PACKAGES[*]} =="
# ryd evt. halv-konfigurerede pakker fra en afbrudt kørsel (samme mønster som 07)
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/dpkg --configure -a
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/apt-get update
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/apt-get install -y --no-install-recommends \
    "${EXTRA_PACKAGES[@]}"
chroot "$ROOTFS" /usr/bin/apt-get clean

echo "== FÆRDIG: ${EXTRA_PACKAGES[*]} er i rootfs'en. Kør 09 for at få dem med i eMMC-imaget. =="
