#!/bin/bash
# 05: Giver SD-kortets Lubuntu statisk IP 192.168.0.50 + ssh ved boot (headless debugging).
# Kør med kortet i læseren: sudo devuan/05_sd_static_ip.sh /dev/sda1
set -euo pipefail
DEV=${1:?Brug: $0 /dev/sdX1}
[ -b "$DEV" ] || { echo "FEJL: $DEV er ikke en blok-enhed"; exit 1; }

MNT=$(mktemp -d)
mount "$DEV" "$MNT"
trap 'umount "$MNT"; rmdir "$MNT"' EXIT

cat > "$MNT/etc/network/interfaces" <<'EOF'
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address 192.168.0.50
    netmask 255.255.255.0
    gateway 192.168.0.1
    dns-nameservers 192.168.0.1
EOF
sync
echo "== OK: $DEV får statisk IP 192.168.0.50 ved næste boot =="
