#!/bin/sh
# myinit.sh — PID 1 shim for GeekBox (vendor-kernel 3.10 + Devuan Excalibur).
# Den gamle 14.04-initramfs flytter ikke /proc, /sys og /dev korrekt ind i det
# nye rootfs, hvilket får sysvinit til at hænge tidligt i rcS. Vi mounter dem
# selv, starter netværk + dropbear (ssh), logger til kortet, og exec'er /sbin/init.
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null
mkdir -p /run/sshd /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null

# Den gamle 14.04-initramfs starter sin EGEN udevd (/sbin/udevd --resolve-names=never),
# og den overlever ind i vores rootfs. rcS starter derefter endnu en, og to daemoner slås
# om netlink-socket'en: udev-databasen (/run/udev/data) bliver aldrig skrevet.
# Følgerne er ubehagelige og ser ud som helt andre fejl:
#   * X får INGEN input-enheder ("The server relies on udev to provide the list of input
#     devices") — mus og tastatur virker ikke, uden en eneste fejlbesked i Xorg.0.log
#   * PulseAudios ALSA-sink kan ikke finde lydkortet og falder tilbage til module-null-sink
#     ("auto_null") — alt spiller lydløst
# Målt på boks 5, 19/8-2026: efter `pkill -9 udevd` + én ren daemon gik databasen fra 0 til
# 206 poster, X hotpluggede musen med det samme, og PA fik sin alsa_output.dmixer.
pkill -9 udevd 2>/dev/null

# netværk: eth0 med DHCP først, ellers statisk fallback — men KUN hvis der er link!
# (ellers efterlades en død default-route på eth0, der kvalte wlan0)
# wlan0 overlades til ifupdown/wpa_supplicant i rcS (undgår dobbelt-dhclient)
# VIGTIGT: interfacet skal OP før carrier kan læses meningsfuldt — på et interface
# der er DOWN læses carrier som 0/EINVAL, så testen alene ville slå eth0 permanent fra
ip link set eth0 up
for i in 1 2 3 4 5; do
    [ "$(cat /sys/class/net/eth0/carrier 2>/dev/null)" = "1" ] && break
    sleep 1
done
if [ "$(cat /sys/class/net/eth0/carrier 2>/dev/null)" = "1" ]; then
    timeout 15 dhclient -1 eth0 2>/dev/null
    if ! ip addr show eth0 | grep -q "inet "; then
        ip addr add 192.168.1.50/24 dev eth0
        ip route add default via 192.168.1.254
    fi
fi
# DNS (boksen har ingen RTC — ved statisk fallback skal nameserver sættes her)
grep -q nameserver /etc/resolv.conf 2>/dev/null || \
    echo "nameserver $(ip route | awk '/default/ {print $3; exit}')" > /etc/resolv.conf

# dropbear virker på 3.10; OpenSSH 10 gør ikke (seccomp-sandbox kræver nyere kernel)
# -s: kun nøgle-login, ingen kodeord
dropbear -s -R -p 22

# fb0-normalisering: EDID-race kan efterlade bpp og stride inkonsistente
# (set: bpp=32 men stride=3840 dvs. 16-bit linjelængde → pixelrod/dobbeltbillede).
# Ground truth er stride: stride/xres = bytes pr. pixel. Tving fb-bpp og X's
# DefaultDepth til at følges ad — så er layout altid konsistent uanset racen.
xres=$(cut -d, -f1 /sys/class/graphics/fb0/virtual_size 2>/dev/null)
stride=$(cat /sys/class/graphics/fb0/stride 2>/dev/null)
if [ "$(( ${stride:-0} / ${xres:-1920} ))" -ge 4 ]; then
    fbset -fb /dev/fb0 -depth 32 -xres 1920 -yres 1080 2>/dev/null
    sed -i "s/DefaultDepth .*/DefaultDepth 24/" /etc/X11/xorg.conf.d/fbdev.conf 2>/dev/null
else
    fbset -fb /dev/fb0 -depth 16 -xres 1920 -yres 1080 2>/dev/null
    sed -i "s/DefaultDepth .*/DefaultDepth 16/" /etc/X11/xorg.conf.d/fbdev.conf 2>/dev/null
fi

# ryd framebufferen (ellers vises boot-logoets rester strakt/pixeleret et øjeblik
# mellem blåt logo og desktop)
dd if=/dev/zero of=/dev/fb0 bs=4M 2>/dev/null

{
  echo "=== myinit $(date) ==="
  echo "--- ip addr:"; ip addr
  echo "--- ip route:"; ip route
  # FULD dmesg (ikke tail): 3.10's compat-lag logger et registerdump pr.
  # clock_gettime64-kald (syscall 403) fra hver 32-bit proces — så snart
  # brugerfladen starter, skylles boot-beskederne ud af dmesg-ringen.
  # Her, før /sbin/init, er loggen endnu intakt (se DEBUG-SORT-SKAERM.md).
  echo "--- dmesg (fuld):"; dmesg
  echo "=== MYINIT-END ==="
} > /root/bootlog.txt 2>&1
sync

exec /sbin/init
