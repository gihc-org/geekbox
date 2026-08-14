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

# netværk: DHCP først, ellers statisk fallback
ip link set eth0 up
timeout 15 dhclient -1 eth0 2>/dev/null
if ! ip addr show eth0 | grep -q "inet "; then
    ip addr add 192.168.1.50/24 dev eth0
    ip route add default via 192.168.1.254
fi
# DNS (boksen har ingen RTC — ved statisk fallback skal nameserver sættes her)
grep -q nameserver /etc/resolv.conf 2>/dev/null || \
    echo "nameserver $(ip route | awk '/default/ {print $3; exit}')" > /etc/resolv.conf

# dropbear virker på 3.10; OpenSSH 10 gør ikke (seccomp-sandbox kræver nyere kernel)
# -s: kun nøgle-login, ingen kodeord
dropbear -s -R -p 22

# fb0-normalisering: vendor-driveren rapporterer tilfældig bpp ift. den faktisk
# allokerede buffer (EDID-race ved boot). Mål reel størrelse, tving var-info til
# at matche, og vælg X-depth derefter (ellers dobbeltbillede/pixelrod).
n=$(dd if=/dev/fb0 of=/dev/null bs=1M 2>&1 | grep -oE "^[0-9]+")
if [ "${n:-0}" -ge 8000000 ]; then
    fbset -fb /dev/fb0 -depth 32 -xres 1920 -yres 1080 2>/dev/null
    sed -i "s/DefaultDepth .*/DefaultDepth 24/" /etc/X11/xorg.conf.d/fbdev.conf 2>/dev/null
else
    fbset -fb /dev/fb0 -depth 16 -xres 1920 -yres 1080 2>/dev/null
    sed -i "s/DefaultDepth .*/DefaultDepth 16/" /etc/X11/xorg.conf.d/fbdev.conf 2>/dev/null
fi

{
  echo "=== myinit $(date) ==="
  echo "--- ip addr:"; ip addr
  echo "--- ip route:"; ip route
  echo "--- dmesg tail:"; dmesg | tail -40
  echo "=== MYINIT-END ==="
} > /root/bootlog.txt 2>&1
sync

exec /sbin/init
