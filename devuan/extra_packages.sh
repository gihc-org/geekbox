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
    locales         # da_DK.UTF-8 — i C-localet dropper X ikke-ASCII ved indtastning (æ/ø/å "virker ikke")
    console-setup   # dansk tastatur på konsollen (tty1-6); /etc/default/keyboard (dk) dækker kun X
    chrony          # NTP — boksen har ingen RTC-batteri; uret starter i 2013 ved hver boot uden denne
    sudo            # 07 lægger kristian i sudo-GRUPPEN, men pakken var aldrig installeret ("sudo: kommandoen ikke fundet")
    rsyslog         # der var INGEN syslog-daemon: nodms fejlbeskeder gik i ingenting, og vi
                    # fejlsøgte en aften i blinde (fuld disk + dødt udev) — se DOK §5.4b.
                    # NB: sysklogd findes IKKE i excalibur ("Unable to locate package").
                    # rsyslog er der i en Devuan-patchet udgave (…devuan1, uden
                    # systemd-afhængigheder) og skriver /var/log/syslog. Alternativer i
                    # repoet, hvis den en dag bliver et problem: busybox-syslogd (lille,
                    # logger til /var/log/messages), syslog-ng, inetutils-syslogd
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

# dansk locale — samme blok som i 01. Denne rootfs er bygget før 01 fik
# locales-pakken, så blokken har aldrig kørt her; ved friske 01-byg er den
# allerede udført (sed/chroot er idempotente). Skal køres i dette script,
# fordi qemu-arm-static stadig ligger i rootfs'en her (fjernes først ved exit).
if [ -x "$ROOTFS/usr/sbin/locale-gen" ]; then
    echo "== genererer da_DK.UTF-8 og sætter det som standard-locale =="
    sed -i "s/^# *da_DK.UTF-8 UTF-8/da_DK.UTF-8 UTF-8/" "$ROOTFS/etc/locale.gen"
    chroot "$ROOTFS" /usr/sbin/locale-gen
    chroot "$ROOTFS" /usr/sbin/update-locale LANG=da_DK.UTF-8
    # konsollens charset skal følge det nye UTF-8-locale (ellers dansk keymap
    # men forkert font/charset på tty1-6). nodm's pam_env læser selv
    # /etc/default/locale, så X-sessionen får LANG automatisk.
    if [ -e "$ROOTFS/usr/bin/setupcon" ]; then
        chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/sbin/dpkg-reconfigure console-setup
    fi
fi

# varm fontconfig-cache: uden den scanner hver GUI-app alle fonte ved FØRSTE
# boot på den flashed'e boks (langsom eMMC + flere apps på én gang = desktop
# står sort i minutter — set som "kun muse-markør efter flash"). Køres her
# fordi qemu-arm-static stadig ligger i rootfs'en (fjernes ved script-exit).
if [ -x "$ROOTFS/usr/bin/fc-cache" ]; then
    echo "== bygger fontconfig-cache (fc-cache -f) =="
    chroot "$ROOTFS" /usr/bin/fc-cache -f
fi

echo "== FÆRDIG: ${EXTRA_PACKAGES[*]} er i rootfs'en. Kør 09 for at få dem med i eMMC-imaget. =="
