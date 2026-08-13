#!/bin/bash
# 06: Installerer dropbear (SSH-server uden seccomp-sandbox) i Devuan-rootfs'en.
# OpenSSH 10's seccomp-sandbox dræber preauth-childen på kernel 3.10 (syscall 397/403 findes ikke).
# Kør: sudo devuan/06_install_dropbear.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$PROJ/devuan/rootfs

cp /usr/bin/qemu-arm-static "$ROOTFS/usr/bin/"
cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"
chroot "$ROOTFS" /usr/bin/apt-get update
chroot "$ROOTFS" /usr/bin/apt-get install -y --no-install-recommends dropbear
rm -f "$ROOTFS/usr/bin/qemu-arm-static"
chroot "$ROOTFS" /usr/bin/apt-get clean
echo "== FÆRDIG: dropbear installeret i rootfs =="
