#!/bin/bash
# 01: Bygger Devuan Excalibur (armhf) rootfs i devuan/rootfs/
# Bruger Devuans egen debootstrap (Ubuntus kender ikke Devuans pakker).
# Kør: sudo devuan/01_build_rootfs.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$PROJ/devuan/rootfs
DBS=$PROJ/devuan/debootstrap-pkg
KEYRING=$PROJ/devuan/keyring/usr/share/keyrings/devuan-archive-keyring.gpg

case "$ROOTFS" in */devuan/rootfs) ;; *) echo "FEJL: uventet ROOTFS=$ROOTFS"; exit 1;; esac

echo "== rydder evt. halvfærdig rootfs =="
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"

echo "== debootstrap første fase (Devuan excalibur, armhf) =="
DEBOOTSTRAP_DIR="$DBS/usr/share/debootstrap" \
    "$DBS/usr/sbin/debootstrap" --foreign --arch=armhf --keyring="$KEYRING" \
    excalibur "$ROOTFS" http://deb.devuan.org/merged

echo "== anden fase via qemu-arm (tager et stykke tid) =="
cp /usr/bin/qemu-arm-static "$ROOTFS/usr/bin/"
cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"
chroot "$ROOTFS" /debootstrap/debootstrap --second-stage

echo "== basis-konfiguration =="
echo geekbox > "$ROOTFS/etc/hostname"
echo 'root:geekbox' | chroot "$ROOTFS" /usr/sbin/chpasswd   # SKIFT efter første login!

# netværk: DHCP på ethernet
cat > "$ROOTFS/etc/network/interfaces" <<'EOF'
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
EOF

# root mountes via label (sat af parameteren på eMMC)
cat > "$ROOTFS/etc/fstab" <<'EOF'
LABEL=sdrootfs1  /  ext4  defaults,noatime  0  1
EOF

# seriel konsol (ttyS2, 115200) — HDMI/tty1-6 er med som standard
touch "$ROOTFS/etc/inittab"
grep -q ttyS2 "$ROOTFS/etc/inittab" || \
    echo 's2:2345:respawn:/sbin/getty -L ttyS2 115200 vt100' >> "$ROOTFS/etc/inittab"

echo "== ekstra pakker (ssh m.m.) =="
chroot "$ROOTFS" /usr/bin/apt-get update
chroot "$ROOTFS" /usr/bin/apt-get install -y --no-install-recommends \
    openssh-server eudev wpasupplicant iputils-ping net-tools htop less vim-tiny
# root-login via ssh til testbrug
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' "$ROOTFS/etc/ssh/sshd_config"

echo "== WiFi-firmware fra vendor =="
mkdir -p "$ROOTFS/lib/firmware" "$ROOTFS/system/etc/firmware"
cp "$PROJ"/vendor_root/system/etc/firmware/* "$ROOTFS/lib/firmware/"
cp "$PROJ"/vendor_root/system/etc/firmware/* "$ROOTFS/system/etc/firmware/"

rm -f "$ROOTFS/usr/bin/qemu-arm-static"
chroot "$ROOTFS" /usr/bin/apt-get clean
echo "== FÆRDIG: rootfs klar i $ROOTFS =="
