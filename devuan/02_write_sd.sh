#!/bin/bash
# 02: Partitionerer SD-kort og lægger Devuan-rootfs på. SLETTER ALT PÅ KORTET.
# Kør: sudo devuan/02_write_sd.sh /dev/sdX
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
DEV=${1:?Brug: $0 /dev/sdX}
NAME=$(basename "$DEV")

[ -b "$DEV" ] || { echo "FEJL: $DEV er ikke en blok-enhed"; exit 1; }
[ "$(cat /sys/block/$NAME/removable)" = "1" ] || { echo "FEJL: $DEV er ikke et flytbart drev — nægter at røre det"; exit 1; }

echo "Skriver til:"
lsblk -d -o NAME,SIZE,MODEL,TRAN "$DEV"
read -rp "ALT på $DEV slettes. Tast JA for at fortsætte: " svar
[ "$svar" = "JA" ] || { echo "afbrudt"; exit 1; }

echo "== afmounter evt. auto-mountede partitioner =="
umount "$DEV"?* 2>/dev/null || true
sleep 1

echo "== partitionering =="
parted -s "$DEV" mklabel msdos
parted -s "$DEV" mkpart primary ext4 4MiB 100%
sleep 2
partprobe "$DEV" || true
sleep 2

echo "== filsystem (ext4 uden features kernel 3.10 ikke kender) =="
mkfs.ext4 -q -L sdrootfs1 -O has_journal,ext_attr,resize_inode,dir_index,filetype,extent,flex_bg,sparse_super,large_file,huge_file,uninit_bg,dir_nlink,extra_isize "${DEV}1"

echo "== kopierer rootfs (tager lidt tid) =="
MNT=$(mktemp -d)
mount "${DEV}1" "$MNT"
cp -a "$PROJ"/devuan/rootfs/. "$MNT"/
sync
umount "$MNT"
rmdir "$MNT"
echo "== FÆRDIG: SD-kort klart =="
