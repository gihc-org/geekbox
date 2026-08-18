#!/bin/bash
# 09: Bygger devuan/update_devuan.img — en update.img hvor Lubuntu-rootfs'en er
# udskiftet med Devuan-rootfs'en (fra 01+06+07) OG parameteren med vores egen
# (root=/dev/mmcblk0p6 + init=/root/myinit.sh), klar til at flashe boksens eMMC
# direkte fra PC'en i loader-tilstand:  upgrade_tool UF devuan/update_devuan.img
# — derefter er alt klart; ingen DI -p, SD-kort eller dd-omvej nødvendig.
#
# Metoden er et "in-place patch": den nye rootfs laves PRÆCIS lige så stor som
# originalens Image/rootfs.img og dd'es ind på dens offset, og parameter-blobben
# (PARM+crc32_rk, bygget af make_parm_bin.py) skrives inden for parameter-entryens
# eksisterende størrelse (nul-paddet). Dermed er alle headere, offsets og
# størrelser i image-filen uændrede, og UF ser en struktur der er byte-identisk
# med den originale update.img — kun to entry'ers indhold skifter.
#
# Kræver at 01+06+07 er kørt. Kør: sudo devuan/09_make_emmc_img.sh
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$PROJ/devuan/rootfs
ORIG=$PROJ/Geekbox_Lubuntu_V160309/Geekbox_Lubuntu_V160309/update.img
OUT=$PROJ/devuan/update_devuan.img
ROOTIMG=$PROJ/devuan/rootfs_devuan.img

[ "$(id -u)" = 0 ] || { echo "FEJL: kør med sudo ($ROOTFS er root-ejet)"; exit 1; }
[ -f "$ROOTFS/root/myinit.sh" ] || { echo "FEJL: $ROOTFS ufuldstændig (mangler root/myinit.sh) — kør 01+06 først"; exit 1; }
[ -d "$ROOTFS/etc/pulse" ] || { echo "FEJL: desktop/lyd mangler i $ROOTFS — kør 07 først"; exit 1; }

# Pakker der viste sig at mangle i ældre rootfs-byg (boks 2 fik dem efterinstalleret
# direkte på p6 — uden dem ender en ny boks uden netværk/fb-korrektion; og uden
# lxterminal står desktoppen uden terminal-emulator):
missing=""
{ [ -e "$ROOTFS/usr/sbin/dhclient" ] || [ -e "$ROOTFS/sbin/dhclient" ]; } || missing="$missing isc-dhcp-client"
{ [ -e "$ROOTFS/usr/bin/fbset" ] || [ -e "$ROOTFS/bin/fbset" ]; } || missing="$missing fbset"
[ -e "$ROOTFS/usr/sbin/wpa_supplicant" ] || missing="$missing wpasupplicant"
[ -e "$ROOTFS/usr/bin/lxterminal" ] || missing="$missing lxterminal"
if [ -n "$missing" ]; then
    echo "FEJL: pakker mangler i $ROOTFS:$missing"
    echo "Læg dem i rootfs'en via devuan/extra_packages.sh (tilføj dem i EXTRA_PACKAGES"
    echo "og genkør det) — eller manuelt med qemu-chroot:"
    echo "  sudo cp /usr/bin/qemu-arm-static $ROOTFS/usr/bin/"
    echo "  sudo cp -L /etc/resolv.conf $ROOTFS/etc/resolv.conf"
    echo "  sudo chroot $ROOTFS /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/apt-get update"
    echo "  sudo chroot $ROOTFS /usr/bin/env DEBIAN_FRONTEND=noninteractive /usr/bin/apt-get install -y --no-install-recommends$missing"
    echo "  sudo rm -f $ROOTFS/usr/bin/qemu-arm-static"
    exit 1
fi

echo "== finder entry-offsets/størrelser i originalen =="
while read -r _name _off _size; do
    case "$_name" in
        Image/rootfs.img) OFF=$_off; SIZE=$_size;;
        parameter)        POFF=$_off; PSIZE=$_size;;
    esac
done < <(python3 - "$ORIG" <<'PYEOF'
import struct, sys
f = open(sys.argv[1], 'rb')
h = f.read(0x66)
assert h[0:4] == b'RKFW', "ikke et RKFW-image"
upd_off = struct.unpack('<I', h[0x21:0x25])[0]
f.seek(upd_off + 0x88)
n = struct.unpack('<I', f.read(4))[0]
found = {}
for i in range(n):
    f.seek(upd_off + 0x8c + i*0x70)
    e = f.read(0x70)
    path = e[0x20:0x40].split(b'\0')[0].decode('latin1')
    off  = struct.unpack('<I', e[0x60:0x64])[0]
    size = struct.unpack('<I', e[0x6c:0x70])[0]
    if path in ('Image/rootfs.img', 'parameter'):
        found[path] = (upd_off + off, size)
for want in ('Image/rootfs.img', 'parameter'):
    if want not in found:
        sys.exit(f"FEJL: fandt ikke {want} i imaget")
    print(want, *found[want])
PYEOF
)
echo "   Image/rootfs.img: offset=$OFF size=$SIZE"
echo "   parameter:        offset=$POFF size=$PSIZE"

echo "== fstab: root-label til linuxroot (eMMC-partitionens label) =="
# Bemærk: ændrer devuan/rootfs/etc/fstab varigt. 01 genskriver den altid til
# sdrootfs1, så en fremtidig SD-bygning (01+02) påvirkes ikke.
sed -i 's/^LABEL=sdrootfs1/LABEL=linuxroot/' "$ROOTFS/etc/fstab"
grep -q '^LABEL=linuxroot' "$ROOTFS/etc/fstab" || { echo "FEJL: fstab-label kunne ikke rettes"; exit 1; }
# NB: swapfil laves på boksen efter resize2fs (2 GB passer ikke i det faste image)

echo "== myinit.sh: altid den aktuelle version fra devuan/ =="
# 06 kopierede engang myinit.sh ind i rootfs'en, men den kopi forældes.
# Imaget skal ALTID have den nyeste (DHCP, carrier-guard, fb-normalisering) —
# en forældet myinit satte fx statisk IP 192.168.1.50 (fejlsøgnings-version).
cp "$PROJ/devuan/myinit.sh" "$ROOTFS/root/myinit.sh"
chmod 755 "$ROOTFS/root/myinit.sh"

echo "== bygger ext4-image af rootfs (label linuxroot, uden features 3.10 ikke kender) =="
rm -f "$ROOTIMG"
truncate -s "$SIZE" "$ROOTIMG"
# Samme feature-liste som script 02 — matcher original-rootfs'ens features præcist.
# -d udfylder imaget med rootfs'ens indhold uden mount (ingen udisks-race).
mkfs.ext4 -q -F -L linuxroot \
    -O has_journal,ext_attr,resize_inode,dir_index,filetype,extent,flex_bg,sparse_super,large_file,huge_file,uninit_bg,dir_nlink,extra_isize,^64bit,^metadata_csum,^metadata_csum_seed,^orphan_file \
    -d "$ROOTFS" "$ROOTIMG"

echo "== kopierer update.img og patcher rootfs-regionen in-place =="
cp "$ORIG" "$OUT"
dd if="$ROOTIMG" of="$OUT" bs=1M oflag=seek_bytes seek="$OFF" conv=notrunc status=none

echo "== bager eMMC-parameteren ind i imaget =="
# Byg binær PARM (PARM + længde + tekst + crc32_rk) ud af tekstfilen.
# Blobben skal være <= parameter-entryens størrelse; resten nul-paddes.
PARMBIN=$PROJ/devuan/parameter_emmc.bin
(cd "$PROJ" && python3 devuan/make_parm_bin.py devuan/parameter_emmc.txt devuan/parameter_emmc.bin)
PARMBYTES=$(wc -c < "$PARMBIN")
if [ "$PARMBYTES" -gt "$PSIZE" ]; then
    echo "FEJL: parameter-blobben er $PARMBYTES bytes, men entryen har kun $PSIZE —"
    echo "      forkort devuan/parameter_emmc.txt (fx fjern en kommentar-linje)"
    exit 1
fi
python3 - "$OUT" "$POFF" "$PSIZE" "$PARMBIN" <<'PYEOF'
import sys
img, poff, psize, binf = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
blob = open(binf, 'rb').read()
blob = blob + b'\0' * (psize - len(blob))
f = open(img, 'r+b')
f.seek(poff)
f.write(blob)
f.close()
print(f"  parameter: {psize} bytes @ offset {poff}")
PYEOF
sync

echo "== verificerer det patchede image mod originalen =="
# Alle entry'er undtagen rootfs og parameter skal være byte-identiske med
# original-imaget; de to patchede sammenlignes mod deres nye kilder.
# Hashing sker i bidder — entry'erne er op til ~1.5 GB, og læses de ind hele
# ad gangen (to kopier af rootfs samtidig) bliver processen OOM-dræbt.
python3 - "$OUT" "$ORIG" "$ROOTIMG" "$PARMBIN" <<'PYEOF'
import struct, sys, hashlib
img, orig, rootimg, parmbin = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

CHUNK = 8 * 1024 * 1024  # 8 MiB ad gangen — konstant, lavt hukommelsesforbrug

def sha256_region(fobj, size):
    h = hashlib.sha256()
    left = size
    while left > 0:
        chunk = fobj.read(min(CHUNK, left))
        if not chunk:
            sys.exit("FEJL: uventet EOF under hashing")
        h.update(chunk)
        left -= len(chunk)
    return h.hexdigest()

f = open(img, 'rb')
o = open(orig, 'rb')
h = f.read(0x66)
assert h[0:4] == b'RKFW'
assert o.read(0x66) == h, "header afviger fra originalen"
upd_off = struct.unpack('<I', h[0x21:0x25])[0]
f.seek(upd_off + 0x88)
n = struct.unpack('<I', f.read(4))[0]
ok = True
for i in range(n):
    f.seek(upd_off + 0x8c + i*0x70)
    e = f.read(0x70)
    path = e[0x20:0x40].split(b'\0')[0].decode('latin1')
    off  = struct.unpack('<I', e[0x60:0x64])[0]
    size = struct.unpack('<I', e[0x6c:0x70])[0]
    if path == 'RESERVED':
        continue  # tom pladsholder (backup-partitionen)
    f.seek(upd_off + off)
    h_img = sha256_region(f, size)
    if path == 'Image/rootfs.img':
        with open(rootimg, 'rb') as r:
            h_ref = sha256_region(r, size)
    elif path == 'parameter':
        blob = open(parmbin, 'rb').read()
        h_ref = hashlib.sha256(blob + b'\0' * (size - len(blob))).hexdigest()
    else:
        o.seek(upd_off + off)
        h_ref = sha256_region(o, size)
    same = h_img == h_ref
    ok &= same
    print(f"  {'OK  ' if same else 'FEJL'} {path} ({size} bytes)")
sys.exit(0 if ok else 1)
PYEOF

echo "== FÆRDIG: $OUT = original boot-kæde + Devuan-rootfs + eMMC-parameter =="
echo
echo "Flash boksen (den skal være i loader-tilstand, USB i OTG-porten):"
echo "  sudo $PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool uf $OUT"
echo "Tag derefter strømmen af/på — boksen booter Devuan direkte fra eMMC"
echo "(parameteren er bagt ind i imaget; hverken DI -p, SD-kort eller dd nødvendig)."
