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
# lightdm må heller ikke have rc-links (26. aug 2026: S04lightdm + S05nodm i rc2.d
# kæmpede om :0 og dræbte X-sessionen på boks 1)
if [ -x "$ROOTFS/usr/sbin/lightdm" ]; then
    chroot "$ROOTFS" update-rc.d -f lightdm remove >/dev/null 2>&1 || true
fi

echo "== bruger + grupper + nøgler =="
chroot "$ROOTFS" /usr/sbin/useradd -m -s /bin/bash kristian || true
# video: /dev/fb0 er root:video, og uden gruppen kan sessionens fb_overscan.py ikke
# åbne framebufferen — den fejler tavst i autostart (fundet 18. aug 2026)
# inet (gid 3003): nødvendig pga. vendor-kernens CONFIG_ANDROID_PARANOID_NETWORK —
# kun root og gruppe-3003-medlemmer kan oprette sockets (målt 26. aug 2026:
# Firefox som kristian fik EACCES på socket() uden den). Fixes også i kernel-
# config (næste byg), men gruppen skal med uanset.
chroot "$ROOTFS" /usr/sbin/groupadd -g 3003 inet || true
chroot "$ROOTFS" /usr/sbin/usermod -aG sudo,input,audio,video,inet kristian
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

Section "ServerFlags"
    # vendor-driverens blank-sti er brød: efter DPMS-powerdown (hdmi remove fra
    # lcdc0) vågner billedet ikke igen — sort skærm med kun markør
    Option "BlankTime" "0"
    Option "StandbyTime" "0"
    Option "SuspendTime" "0"
    Option "OffTime" "0"
EndSection
EOF

# pcmanfm's standard-wallpaper (/etc/xdg/pcmanfm/LXDE/pcmanfm.conf) peger på
# /etc/alternatives/desktop-background, som ejes af desktop-base — uden den
# pakke bliver skrivebordsbaggrunden sort. Peg linket på LXDE's egen baggrund.
ln -sf /usr/share/lxde/wallpapers/lxde_blue.jpg "$ROOTFS/etc/alternatives/desktop-background"

# overscan-kompensation: TV'et beskærer ~2,3 % på alle fire kanter, så panelet i
# bunden forsvinder. Kernens egen kompensation er død kode; fb-var'ens grayscale/nonstd
# virker. Se devuan/fb_overscan.py. Skal køre EFTER X, derfor lxsession-autostart —
# uden @, den skal kun køre én gang.
install -D -m 755 "$PROJ/devuan/fb_overscan.py" "$ROOTFS/usr/local/bin/fb_overscan.py"
AUTOSTART=$ROOTFS/etc/xdg/lxsession/LXDE/autostart
mkdir -p "$(dirname "$AUTOSTART")"
grep -q fb_overscan "$AUTOSTART" 2>/dev/null || \
    echo "/usr/local/bin/fb_overscan.py --percent 95" >> "$AUTOSTART"

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
ALSA_DA_LIB=/opt/alsa-da/usr/lib/arm-linux-gnueabihf
# /etc/profile.d dækker kun login-shells (ssh, tty), IKKE skrivebordet. Beholdes alligevel:
# den er nyttig når man tester over ssh.
echo "export LD_LIBRARY_PATH=$ALSA_DA_LIB\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" \
    > "$ROOTFS/etc/profile.d/alsa-legacy.sh"
# Det sted der virker for skrivebordet: pulseaudios EGEN startlinje. Kun PA får det gamle
# bibliotek, og resten af sessionen kører uberørt.
# Verificeret på boks 5 (19/8-2026): PA's /proc/<pid>/maps viser libasound fra
# /opt/alsa-da, og sinken er alsa_output.dmixer i stedet for auto_null.
#
# To andre steder blev prøvet og forkastet:
#   * /etc/environment (pam_env): nodm's session får den ikke. Testen der "beviste" at den
#     virkede var en måleartefakt — min egen `su kristian -c pactl` startede en NY
#     pulseaudio, og `su` læser /etc/environment. Mål altid på den PA sessionen selv har
#     startet: grep alsa-da /proc/$(pgrep -x pulseaudio)/maps
#   * /etc/X11/Xsession.d/-snippet: FARLIG. Den tvinger det gamle bibliotek ned over hele
#     sessionen, og biblioteket mangler symboler nyere programmer kræver (aplay:
#     "undefined symbol: snd_pcm_subformat_value"). Sessionen døde af det.
PA_AUTOSTART=$ROOTFS/etc/xdg/autostart/pulseaudio.desktop
if [ -f "$PA_AUTOSTART" ]; then
    grep -q "^Exec=env LD_LIBRARY_PATH" "$PA_AUTOSTART" || \
        sed -i "s|^Exec=|Exec=env LD_LIBRARY_PATH=$ALSA_DA_LIB |" "$PA_AUTOSTART"
    grep -q "^Exec=env LD_LIBRARY_PATH" "$PA_AUTOSTART" || \
        { echo "FEJL: kunne ikke sætte LD_LIBRARY_PATH på PA's Exec-linje"; exit 1; }
else
    echo "FEJL: $PA_AUTOSTART findes ikke — er pulseaudio installeret?"; exit 1
fi
# NB: lyd kræver OGSÅ at udev virker. Er der to udevd'er (initramfs + rcS), bliver
# udev-databasen tom, og PA's alsa-sink falder tilbage til auto_null uanset biblioteksstien.
# myinit.sh dræber initramfs-udevd af netop den grund — se DOKUMENTATION.md §5.4b.
# Test lyden med PA, ikke med aplay (som ikke kan køre mod det gamle bibliotek):
#   pactl list sinks short   →  skal vise alsa_output.dmixer, ikke auto_null

echo "== diverse rettelser lært undervejs =="
# systemd-sysusers fejler på 3.10 (EINVAL lock) — postinsts skal bruge adduser-stien
chroot "$ROOTFS" dpkg-divert --add --rename /usr/bin/systemd-sysusers || true
# openssh-server kan ikke køre på 3.10 (seccomp) — slå dens boot-service fra (dropbear kører)
chroot "$ROOTFS" /usr/sbin/update-rc.d ssh disable || true

rm -f "$ROOTFS/usr/bin/qemu-arm-static"
chroot "$ROOTFS" /usr/bin/apt-get clean
echo "== FÆRDIG: desktop + lyd i rootfs. Kør nu 02 for at skrive kortet. =="
