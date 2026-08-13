#!/bin/sh
# myinit.sh — køres som PID 1 (init=/root/myinit.sh) til headless fejlsøgning.
# Logger boot-tilstand til SD, starter netværk + ssh, og kalder derefter /sbin/init.
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null
mkdir -p /run/sshd /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null

# netværk (statisk — uafhængig af DHCP)
ip link set eth0 up
ip addr add 192.168.1.50/24 dev eth0
ip route add default via 192.168.1.254

# ssh så tidligt som overhovedet muligt
# dropbear på port 22 (virker på 3.10); openssh på 2222 fejler pga. seccomp på gammel kernel
dropbear -R -p 22
/usr/sbin/sshd -ddd -p 2222 -E /root/sshd.log

{
  echo "=== myinit $(date) ==="
  echo "--- ip addr:"; ip addr
  echo "--- ip route:"; ip route
  echo "--- dmesg tail:"; dmesg | tail -60
  echo "=== MYINIT-END ==="
} > /root/bootlog.txt 2>&1
sync

# videre til normal boot (hænger den, har vi stadig ssh + loggen)
exec /sbin/init
