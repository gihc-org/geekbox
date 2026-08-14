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

# netværk: DHCP på ethernet + wifi (wpa-credentials udfyldes på boksen)
cat > "$ROOTFS/etc/network/interfaces" <<'EOF'
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp

auto wlan0
iface wlan0 inet dhcp
    wpa-driver nl80211
    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf
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
    openssh-server eudev wpasupplicant iputils-ping net-tools htop less vim-tiny \
    isc-dhcp-client wireless-tools locales chrony fbset
# root-login via ssh til testbrug
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' "$ROOTFS/etc/ssh/sshd_config"

# dhclient-timeout: et kabel-løst eth0 må ikke blokere wlan0 i ifupdown-rækken
echo "timeout 15;" >> "$ROOTFS/etc/dhcp/dhclient.conf"

# dansk locale + tastatur + tidszone
sed -i "s/^# *da_DK.UTF-8 UTF-8/da_DK.UTF-8 UTF-8/" "$ROOTFS/etc/locale.gen"
chroot "$ROOTFS" /usr/sbin/locale-gen
chroot "$ROOTFS" /usr/sbin/update-locale LANG=da_DK.UTF-8
ln -sf /usr/share/zoneinfo/Europe/Copenhagen "$ROOTFS/etc/localtime"
echo "Europe/Copenhagen" > "$ROOTFS/etc/timezone"
sed -i "s/^XKBLAYOUT=.*/XKBLAYOUT=\"dk\"/" "$ROOTFS/etc/default/keyboard" 2>/dev/null || true

echo "== WiFi-firmware fra vendor =="
mkdir -p "$ROOTFS/lib/firmware" "$ROOTFS/system/etc/firmware"
cp "$PROJ"/vendor_root/system/etc/firmware/* "$ROOTFS/lib/firmware/"
cp "$PROJ"/vendor_root/system/etc/firmware/* "$ROOTFS/system/etc/firmware/"

rm -f "$ROOTFS/usr/bin/qemu-arm-static"
chroot "$ROOTFS" /usr/bin/apt-get clean
echo "== FÆRDIG: rootfs klar i $ROOTFS =="
