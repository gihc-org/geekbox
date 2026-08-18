#!/bin/bash
# emmc_first_boot.sh: Efterbehandling af en friskflashet eMMC-boks. Køres PÅ
# boksen (IKKE PC'en!) som root over ssh efter første boot af update_devuan.img:
#
#   ssh -i ~/.ssh/geekbox_key root@<IP> 'bash -s' < devuan/emmc_first_boot.sh
#
# Gør to ting:
#   1. resize2fs: rootfs'en i imaget er kun ~1,4 GB (09 er låst til originalens
#      Image/rootfs.img-størrelse) — her udvides den til hele p6 (~15 GB).
#   2. 2 GB swapfil + fstab-linje + swapon. Swapfilen kan ikke ligge i imaget:
#      der er hverken plads i det faste image eller i det 1,4 GB store
#      filsystem før resize2fs — derfor er dette et separat efter-trin.
#      (2 GB RAM på boksen er ikke nok til tunge apps som firefox uden swap.)
# Idempotent — kan genkøres frit.
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "FEJL: kør som root"; exit 1; }
[ -b /dev/mmcblk0p6 ] || { echo "FEJL: /dev/mmcblk0p6 findes ikke — dette script hører til på eMMC-boksen"; exit 1; }

echo "== resize2fs: rootfs udvides til hele /dev/mmcblk0p6 =="
resize2fs /dev/mmcblk0p6   # "Nothing to do" hvis den allerede er udvidet — helt fint

echo "== 2 GB swapfil =="
if grep -q '^/swapfile ' /proc/swaps; then
    echo "   /swapfile er allerede aktiv — springer oprettelse over"
else
    dd if=/dev/zero of=/swapfile bs=1M count=2048 status=none   # tager et stykke tid på eMMC
    chmod 600 /swapfile
    mkswap /swapfile >/dev/null
fi
grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
swapon -a

echo "== FÆRDIG =="
df -h /
swapon --show
