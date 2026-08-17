#!/bin/bash
# 07: Desktop (X/LXDE via fbdev + nodm) og lyd (legacy alsa + dmix) i rootfs'en.
# Kører via qemu-chroot på PC'en — bevidst IKKE på boksen: postinst-scripts der bruger
# systemd-sysusers fejler på 3.10-kernen, men virker fint under PC'ens kernel.
# Kør efter 01+06: sudo devuan/07_desktop_audio.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$PROJ/devuan/rootfs

cp /usr/bin/qemu-arm-static "$ROOTFS/usr/bin/"
cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"

echo "== desktop- og lydpakker (headless) =="
# Ingen interaktive debconf-stop: noninteractive-frontend + preseed af de to
# spørgsmål der ellers afbryder kørslen (tastatur-layout + valg af display manager)
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/debconf-set-selections <<'EOF'
keyboard-configuration	keyboard-configuration/layoutcode	string	dk
keyboard-configuration	keyboard-configuration/modelcode	string	pc105
console-setup	console-setup/charmap47	select	UTF-8
nodm	shared/default-x-display-manager	select	nodm
lightdm	shared/default-x-display-manager	select	nodm
EOF
# ryd evt. halv-konfigurerede pakker fra en afbrudt kørsel
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/dpkg --configure -a
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/apt-get update
chroot "$ROOTFS" /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/apt-get install -y --no-install-recommends \
    xserver-xorg xserver-xorg-video-fbdev xserver-xorg-legacy xinit \
    lxde-core nodm pulseaudio pavucontrol alsa-utils
# nodm skal være default display manager, uanset debconf-defaults (lightdm's
# logind-seat-detektion virker ikke på denne boks — derfor nodm)
echo /usr/sbin/nodm > "$ROOTFS/etc/X11/default-display-manager"

echo "== bruger + grupper + nøgler =="
chroot "$ROOTFS" /usr/sbin/useradd -m -s /bin/bash kristian || true
chroot "$ROOTFS" /usr/sbin/usermod -aG sudo,input,audio kristian
echo 'kristian:geekbox' | chroot "$ROOTFS" /usr/sbin/chpasswd   # midlertidig — SKIFT!
for u in root kristian; do
    home=$([ "$u" = root ] && echo /root || echo /home/kristian)
    mkdir -p "$ROOTFS$home/.ssh"
    touch "$ROOTFS$home/.ssh/authorized_keys"
    # append kun nøgler der ikke allerede ligger der (ellers dubletter ved genkørsel)
    while IFS= read -r key; do
        [ -z "$key" ] && continue
        grep -qxF "$key" "$ROOTFS$home/.ssh/authorized_keys" || echo "$key" >> "$ROOTFS$home/.ssh/authorized_keys"
    done < "$PROJ"/devuan/authorized_keys
    chroot "$ROOTFS" chown -R "$u":"$u" "$home/.ssh" 2>/dev/null || true
    chroot "$ROOTFS" chmod 700 "$home/.ssh" 2>/dev/null || true
    chroot "$ROOTFS" chmod 600 "$home/.ssh/authorized_keys" 2>/dev/null || true
done

echo "== nodm (autologin) + Xorg som ikke-root =="
sed -i -e "s/^NODM_ENABLED=.*/NODM_ENABLED=true/" -e "s/^NODM_USER=.*/NODM_USER=kristian/" "$ROOTFS/etc/default/nodm"
echo "allowed_users=anybody" > "$ROOTFS/etc/X11/Xwrapper.config"
mkdir -p "$ROOTFS/etc/X11/xorg.conf.d"
cat > "$ROOTFS/etc/X11/xorg.conf.d/fbdev.conf" <<'EOF'
# GeekBox RK3368: fbdev (bpp normaliseres af /root/myinit.sh ved boot)
Section "Device"
    Identifier "Framebuffer"
    Driver "fbdev"
EndSection

Section "Screen"
    Identifier "Default Screen"
    Device "Framebuffer"
    DefaultDepth 16
EndSection
EOF

echo "== lyd: legacy libasound (32-bit time) + vendor dmix + PA-sink =="
# daedalus' libasound2: trixies t64-variant bruger ioctls 3.10 ikke kender (ENOTTY)
tmp=$(mktemp -d)
wget -q "http://deb.devuan.org/merged/pool/DEBIAN/main/a/alsa-lib/libasound2_1.2.8-1+b1_armhf.deb" -O "$tmp/libasound2.deb"
mkdir -p "$ROOTFS/opt/alsa-da"
dpkg-deb -x "$tmp/libasound2.deb" "$ROOTFS/opt/alsa-da"
rm -rf "$tmp"
# vendor-driverens write-sti er død; kun mmap via dmix virker — brug vendors asound.conf
cp "$PROJ/vendor_root/etc/asound.conf" "$ROOTFS/etc/asound.conf"
# PA skal bruge dmix-enheden, ikke de tavse direkte-hw sinks
sed -i "s|^load-module module-udev-detect.*|load-module module-alsa-sink device=dmixer|" "$ROOTFS/etc/pulse/default.pa"
echo 'export LD_LIBRARY_PATH=/opt/alsa-da/usr/lib/arm-linux-gnueabihf${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}' \
    > "$ROOTFS/etc/profile.d/alsa-legacy.sh"

echo "== diverse rettelser lært undervejs =="
# systemd-sysusers fejler på 3.10 (EINVAL lock) — postinsts skal bruge adduser-stien
chroot "$ROOTFS" dpkg-divert --add --rename /usr/bin/systemd-sysusers || true
# openssh-server kan ikke køre på 3.10 (seccomp) — slå dens boot-service fra (dropbear kører)
chroot "$ROOTFS" /usr/sbin/update-rc.d ssh disable || true

rm -f "$ROOTFS/usr/bin/qemu-arm-static"
chroot "$ROOTFS" /usr/bin/apt-get clean
echo "== FÆRDIG: desktop + lyd i rootfs. Kør nu 02 for at skrive kortet. =="
