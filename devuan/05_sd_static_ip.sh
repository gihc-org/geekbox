#!/bin/bash
# 05: Giver SD-kortets Lubuntu statisk IP + ssh ved boot (headless debugging).
# Kør med kortet i læseren: sudo devuan/05_sd_static_ip.sh /dev/sda1 [IP] [GATEWAY]
# Standard-IP: 192.168.0.50 (gateway antages at være .1 på samme subnet, medmindre angivet)
set -euo pipefail
DEV=${1:?Brug: $0 /dev/sdX1 [IP] [GATEWAY]}
IP=${2:-192.168.0.50}
GW=${3:-$(echo "$IP" | sed -E 's/\.[0-9]+$/.1/')}
[ -b "$DEV" ] || { echo "FEJL: $DEV er ikke en blok-enhed"; exit 1; }
echo "$IP" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' || { echo "FEJL: ugyldig IP '$IP'"; exit 1; }

MNT=$(mktemp -d)
mount "$DEV" "$MNT"
trap 'umount "$MNT"; rmdir "$MNT"' EXIT

cat > "$MNT/etc/network/interfaces" <<EOF
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address $IP
    netmask 255.255.255.0
    gateway $GW
    dns-nameservers $GW
EOF
sync
echo "== OK: $DEV får statisk IP $IP (gateway $GW) ved næste boot =="
