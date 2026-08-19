#!/bin/bash
# 09: Bygger devuan/update_devuan.img — en update.img hvor Lubuntu-rootfs'en er
# udskiftet med Devuan-rootfs'en (fra 01+06+07), parameteren med vores egen
# (root=/dev/mmcblk0p6 + init=/root/myinit.sh) og DTB'ens uboot-logo-flag slået fra
# (fixer sort skærm/manglende panel efter flash), klar til at flashe boksens eMMC
# direkte fra PC'en i loader-tilstand:  upgrade_tool UF devuan/update_devuan.img
# — derefter er alt klart; ingen DI -p, SD-kort eller dd-omvej nødvendig.
#
# Metoden er et "in-place patch": den nye rootfs laves PRÆCIS lige så stor som
# originalens Image/rootfs.img og dd'es ind på dens offset, parameter-blobben
# (PARM+crc32_rk, bygget af make_parm_bin.py) skrives inden for parameter-entryens
# eksisterende størrelse (nul-paddet), og DTB-patchen skifter ét FDT-ord i
# Image/ramfs.img og Image/resource.img. Dermed er alle headere, offsets og
# størrelser i image-filen uændrede, og UF ser en struktur der er byte-identisk
# med den originale update.img — kun fire entry'ers indhold skifter.
#
# Kræver at 01+06+07 er kørt. Kør: sudo devuan/09_make_emmc_img.sh
set -euo pipefail
# Hvilken DT-ændring bages ind i imagets to DTB-kopier? (se blokken længere nede)
#   policy = rockchip,disp-policy 2 -> 0   (standard; kernen kalder selv load_screen)
#   logo   = rockchip,uboot-logo-on 1 -> 0  ADVARSEL: boksen booter IKKE (målt 18/8-2026)
#   none   = ingen DTB-ændring (som originalen)
DTB_PATCH=${DTB_PATCH:-policy}
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
# dansk locale: uden den dropper X æ/ø/å ved indtastning (C-locale). /etc/default/locale
# skrives af extra_packages.sh når locales installeres (rootfs fra før 01's locales-pakke).
[ -e "$ROOTFS/etc/default/locale" ] || missing="$missing locales console-setup"
# chrony: boksen har ingen RTC-batteri — uden NTP starter uret i 2013 ved hver boot
[ -e "$ROOTFS/usr/sbin/chronyd" ] || missing="$missing chrony"
# sudo: 07 lægger kristian i sudo-gruppen, men pakken skal også være der — ellers
# svarer boksen "sudo: kommandoen ikke fundet" (fundet på boks 4, aug 2026)
[ -e "$ROOTFS/usr/bin/sudo" ] || missing="$missing sudo"
# syslog: uden en daemon går nodms og andres fejlbeskeder i ingenting, og så fejlsøger man
# i blinde — det kostede en aften (fuld disk + dødt udev, begge tavse). Se DOK §5.4b.
{ [ -e "$ROOTFS/usr/sbin/syslogd" ] || [ -e "$ROOTFS/usr/sbin/rsyslogd" ]; } || missing="$missing rsyslog"
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
        Image/rootfs.img)   OFF=$_off;  SIZE=$_size;;
        parameter)          POFF=$_off; PSIZE=$_size;;
        Image/ramfs.img)    BOFF=$_off; BSIZE=$_size;;
        Image/resource.img) ROFF=$_off; RSIZE=$_size;;
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
WANTED = ('Image/rootfs.img', 'parameter', 'Image/ramfs.img', 'Image/resource.img')
for i in range(n):
    f.seek(upd_off + 0x8c + i*0x70)
    e = f.read(0x70)
    path = e[0x20:0x40].split(b'\0')[0].decode('latin1')
    off  = struct.unpack('<I', e[0x60:0x64])[0]
    size = struct.unpack('<I', e[0x6c:0x70])[0]
    if path in WANTED:
        found[path] = (upd_off + off, size)
for want in WANTED:
    if want not in found:
        sys.exit(f"FEJL: fandt ikke {want} i imaget")
    print(want, *found[want])
PYEOF
)
echo "   Image/rootfs.img:   offset=$OFF size=$SIZE"
echo "   parameter:          offset=$POFF size=$PSIZE"
echo "   Image/ramfs.img:    offset=$BOFF size=$BSIZE"
echo "   Image/resource.img: offset=$ROFF size=$RSIZE"

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

echo "== skrivebordsbaggrund: symlink til LXDE's tapet =="
# pcmanfm's wallpaper peger på /etc/alternatives/desktop-background, som ejes af
# desktop-base — en pakke vi ikke installerer. Uden linket er skrivebordet SORT, og et
# sort skrivebord kan ikke skelnes fra et defekt display: det sendte en hel
# fejlsøgningsdag efter et "afkortet billede" der ikke fandtes (DEBUG-SORT-SKAERM.md).
# 07 sætter linket i nye byg; her sikres det også for ældre rootfs'er. Idempotent.
WALLPAPER=/usr/share/lxde/wallpapers/lxde_blue.jpg
# NB: kontrollér målet MED $ROOTFS-præfiks. Symlinket er absolut inde i rootfs'en, så
# `test -e` på selve linket følger stien på VÆRTEN og fejler altid herfra.
[ -e "$ROOTFS$WALLPAPER" ] || \
    { echo "FEJL: $WALLPAPER mangler i rootfs'en (lxde-common ikke installeret?)"; exit 1; }
ln -sf "$WALLPAPER" "$ROOTFS/etc/alternatives/desktop-background"
[ -L "$ROOTFS/etc/alternatives/desktop-background" ] || \
    { echo "FEJL: symlinket til skrivebordsbaggrunden blev ikke oprettet"; exit 1; }

echo "== overscan-kompensation i sessionen =="
# TV'et beskærer ~2,3 % på alle fire kanter (~25 linjer top/bund, ~48 px i siderne), så
# lxpanel i bunden forsvinder helt. Kernens egen kompensation er død kode
# (rk_fb_disp_scale returnerer straks når HDMI er primær skærm), men fb-var'ens
# grayscale/nonstd virker — se devuan/fb_overscan.py. SKAL køre efter X er startet,
# derfor lxsession-autostart (uden @: den skal kun køre én gang).
install -D -m 755 "$PROJ/devuan/fb_overscan.py" "$ROOTFS/usr/local/bin/fb_overscan.py"
# /dev/fb0 er root:video — uden gruppen fejler scriptet tavst i autostart.
# 07 sætter gruppen ved useradd; her sikres det også for ældre rootfs'er (idempotent).
python3 - "$ROOTFS/etc/group" <<'PYEOF'
import sys
sti = sys.argv[1]
linjer = open(sti).read().splitlines()
ud = []
for linje in linjer:
    felt = linje.split(":")
    if felt[0] == "video" and len(felt) == 4:
        medlemmer = [m for m in felt[3].split(",") if m]
        if "kristian" not in medlemmer:
            medlemmer.append("kristian")
        felt[3] = ",".join(medlemmer)
        linje = ":".join(felt)
    ud.append(linje)
open(sti, "w").write("\n".join(ud) + "\n")
PYEOF
grep -q "^video:.*kristian" "$ROOTFS/etc/group" || \
    { echo "FEJL: kunne ikke føje kristian til video-gruppen i rootfs'en"; exit 1; }
AUTOSTART=$ROOTFS/etc/xdg/lxsession/LXDE/autostart
if [ -f "$AUTOSTART" ]; then
    grep -q fb_overscan "$AUTOSTART" || \
        echo "/usr/local/bin/fb_overscan.py --percent 95" >> "$AUTOSTART"
else
    echo "FEJL: $AUTOSTART findes ikke — er lxsession installeret? (kør 07)"; exit 1
fi

echo "== lyd: pulseaudio startes med daedalus-libasound =="
# 3.10 kender ikke de 64-bit-time ioctls som trixies libasound2t64 bruger (ENOTTY ved
# open), så PA kan ikke åbne ALSA og falder tilbage til module-null-sink ("auto_null") —
# alt spiller lydløst. Stien til det gamle bibliotek skal sidde på PA's EGEN startlinje;
# /etc/profile.d og /etc/environment rækker ikke (sessionen får dem ikke), og en
# Xsession.d-snippet er farlig (tvinger det gamle bibliotek ned over hele sessionen, som
# så dør — biblioteket mangler symboler nyere programmer kræver).
# 07 sætter linjen i nye byg; her sikres det også for ældre rootfs'er (idempotent).
# NB: lyd kræver OGSÅ at udev virker — se myinit's pkill af initramfs-udevd.
PA_AUTOSTART=$ROOTFS/etc/xdg/autostart/pulseaudio.desktop
[ -f "$PA_AUTOSTART" ] || { echo "FEJL: $PA_AUTOSTART mangler — er pulseaudio installeret? (kør 07)"; exit 1; }
[ -e "$ROOTFS/opt/alsa-da/usr/lib/arm-linux-gnueabihf/libasound.so.2" ] || \
    { echo "FEJL: daedalus-libasound mangler i $ROOTFS/opt/alsa-da — kør 07"; exit 1; }
grep -q "^Exec=env LD_LIBRARY_PATH" "$PA_AUTOSTART" || \
    sed -i "s|^Exec=|Exec=env LD_LIBRARY_PATH=/opt/alsa-da/usr/lib/arm-linux-gnueabihf |" "$PA_AUTOSTART"
grep -q "^Exec=env LD_LIBRARY_PATH=/opt/alsa-da" "$PA_AUTOSTART" || \
    { echo "FEJL: kunne ikke sætte LD_LIBRARY_PATH på pulseaudios Exec-linje"; exit 1; }

echo "== syslog: filtrér 3.10's syscall-403-flod fra =="
# 3.10's compat-lag logger et KOMPLET registerdump for HVERT kald til clock_gettime64
# (armhf-syscall 403), som glibc 2.41 kalder konstant fra alle 32-bit processer.
# Målt uden filter: ~30 MB/time skrevet til eMMC'en (syslog + kern.log), og alle rigtige
# beskeder druknede. Med filteret: 0 linjer på 45 sekunder, og `logger` kommer stadig igennem.
if [ -e "$ROOTFS/usr/sbin/rsyslogd" ]; then
    mkdir -p "$ROOTFS/etc/rsyslog.d"
    cat > "$ROOTFS/etc/rsyslog.d/05-drop-compat-syscall-flood.conf" <<'EOF'
# Se DOKUMENTATION.md og HAANDBOG.md: 3.10 dumper alle registre ved hvert kald til den
# ukendte syscall 403 (clock_gettime64). Det er KERN_WARNING, så prioritet kan ikke bruges
# til at filtrere. Vi dropper de linjeformer dumpet består af.
# NB: ereregex, IKKE regex — rsyslogs "regex" er POSIX BRE, hvor + er et almindeligt tegn
# og (a|b) ikke virker. Det kostede en runde at opdage.
# Rigtige oops beholdes: "BUG:", "Internal error", "Unable to handle kernel" rammes ikke.
:msg, contains, "syscall 403" stop
:msg, contains, "do_ni_syscall" stop
:msg, contains, "PC is at " stop
:msg, contains, "LR is at " stop
:msg, contains, "Code: " stop
:msg, ereregex, "\] *x[0-9]+ *:" stop
:msg, ereregex, "\] *(pc|lr|sp|pstate) *:" stop
:msg, ereregex, "\] *task: [0-9a-f]+" stop
:msg, ereregex, "\] *CPU: [0-9]+ PID: [0-9]+ Comm:" stop
:msg, ereregex, "^ *\[[0-9]+:[^]]*\] *$" stop
EOF
    echo "   filteret er på plads"
else
    echo "   (rsyslog er ikke i rootfs'en — springer over)"
fi

echo "== netværk: NetworkManager må styres uden root =="
# Uden dette kan brugeren se wifi-netværk i nm-applet, men ikke tilslutte sig: polkit
# afviser, fordi nodm starter X UDEN en logind-session, og polkits standardregler kræver
# en "aktiv session". Løsningen er en regel der giver ja ud fra gruppemedlemskab i stedet.
# eth0 bliver på ifupdown (myinit's tidlige bring-up + ssh-stien er uændret); wlan0 står
# ikke i /etc/network/interfaces, og NM overtager derfor selv den.
if [ -e "$ROOTFS/usr/sbin/NetworkManager" ]; then
    python3 - "$ROOTFS/etc/group" <<'PYEOF'
import sys
sti = sys.argv[1]
ud = []
fundet = False
for linje in open(sti).read().splitlines():
    felt = linje.split(":")
    if felt[0] == "netdev" and len(felt) == 4:
        fundet = True
        medlemmer = [m for m in felt[3].split(",") if m]
        if "kristian" not in medlemmer:
            medlemmer.append("kristian")
        felt[3] = ",".join(medlemmer)
        linje = ":".join(felt)
    ud.append(linje)
if not fundet:
    ud.append("netdev:x:104:kristian")
open(sti, "w").write("\n".join(ud) + "\n")
PYEOF
    grep -q "^netdev:.*kristian" "$ROOTFS/etc/group" || \
        { echo "FEJL: kunne ikke føje kristian til netdev-gruppen"; exit 1; }
    mkdir -p "$ROOTFS/etc/polkit-1/rules.d"
    cat > "$ROOTFS/etc/polkit-1/rules.d/50-nm-netdev.rules" <<'EOF'
// Lad medlemmer af netdev styre NetworkManager uden adgangskode.
// Nødvendigt fordi nodm starter X uden logind-session: polkit ser ingen aktiv session,
// og standardreglerne kræver netop en sådan. Se DOKUMENTATION.md og HAANDBOG.md.
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.isInGroup("netdev")) {
        return polkit.Result.YES;
    }
});
EOF
    grep -q "wlan0" "$ROOTFS/etc/network/interfaces" && \
        echo "   ADVARSEL: wlan0 står i /etc/network/interfaces — NM lader den så i fred"
    echo "   netdev-gruppen og polkit-reglen er på plads"
else
    echo "   (NetworkManager er ikke i rootfs'en — springer over. Læg den i extra_packages.sh)"
fi

echo "== pladsvagt: kan rootfs'en være i imaget? =="
# Imagets rootfs er LÅST til originalens størrelse (~1408 MiB) — den kan ikke gøres større
# uden at bryde in-place-metoden. Med firefox-esr er den fyldt ~84 %, så næste store pakke
# kan vælte den. Uden denne vagt fejler mkfs.ext4 midt i bygningen med et kryptisk
# "No space left on device"; her får man tallene i stedet.
# 64 MiB holdes fri til ext4-metadata (inode-tabeller, journal) og root-reserve.
brugt=$(du -sxb "$ROOTFS" | awk '{print $1}')
margin=$((64 * 1024 * 1024))
printf "   rootfs-indhold: %d MiB   image: %d MiB   margin: %d MiB\n" \
    $((brugt / 1048576)) $((SIZE / 1048576)) $((margin / 1048576))
if [ "$brugt" -gt "$((SIZE - margin))" ]; then
    echo "FEJL: rootfs'en fylder $((brugt / 1048576)) MiB og kan ikke være i et image på"
    echo "      $((SIZE / 1048576)) MiB (mindst $((margin / 1048576)) MiB skal være fri til ext4-metadata)."
    echo "      Fjern pakker i devuan/extra_packages.sh, eller ryd op i rootfs'en:"
    echo "        sudo chroot $ROOTFS apt-get clean"
    echo "        sudo du -sxh $ROOTFS/* | sort -h | tail"
    exit 1
fi

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

echo "== DTB-patch: $DTB_PATCH (begge kopier) =="
# Formålet: få vendor-kernen til selv at programmere skærm-timingen. Uden en af disse
# ændringer kalder den ALDRIG load_screen() på den primære LCDC — den arver U-Boots
# registre og retter dem kun hvis HDMI'ens opløsning tilfældigvis afviger.
# Se devuan/patch_uboot_logo.py (kildehenvisninger) og DEBUG-SORT-SKAERM.md.
#
# ADVARSEL, målt 18. aug 2026: rockchip,uboot-logo-on = 0 gør at boksen IKKE BOOTER
# (LED'en bliver lilla og aldrig blå — hænger i U-Boot eller meget tidligt i kernen).
# Samme flag styrer nemlig OGSÅ loaderens egen display-init via board_fbt_preboot(),
# hvis definition ikke findes i vores U-Boot-kildetræ. Brug derfor policy-varianten:
# rockchip,disp-policy 2 (BOX_TEMP) -> 0 (SDK) rammer kun kernens beslutning
# (rk_fb.c:3559's tredje betingelse) og lader loaderens logo-sti i fred.
#
# To kopier af DTB'en findes: U-Boot bruger BOOT-partitionens (Image/ramfs.img,
# second-arealet) og falder tilbage til resource-partitionen — begge patches.
BOOTIMG=$PROJ/devuan/ramfs_patched.img
RESIMG=$PROJ/devuan/resource_patched.img
case "$DTB_PATCH" in
    policy) PATCHARGS=(--prop rockchip,disp-policy --value 0);;
    logo)   PATCHARGS=(--prop rockchip,uboot-logo-on --value 0)
            echo "   ADVARSEL: 'logo'-varianten gav en boks der ikke booter (18. aug 2026)";;
    none)   PATCHARGS=();;
    *)      echo "FEJL: DTB_PATCH skal være policy, logo eller none (er: $DTB_PATCH)"; exit 1;;
esac
dd if="$ORIG" of="$BOOTIMG" bs=1M iflag=skip_bytes,count_bytes skip="$BOFF" count="$BSIZE" status=none
dd if="$ORIG" of="$RESIMG"  bs=1M iflag=skip_bytes,count_bytes skip="$ROFF" count="$RSIZE" status=none
if [ ${#PATCHARGS[@]} -gt 0 ]; then
    python3 "$PROJ/devuan/patch_uboot_logo.py" "$BOOTIMG" "${PATCHARGS[@]}"
    python3 "$PROJ/devuan/patch_uboot_logo.py" "$RESIMG"  "${PATCHARGS[@]}"
fi
dd if="$BOOTIMG" of="$OUT" bs=1M oflag=seek_bytes seek="$BOFF" conv=notrunc status=none
dd if="$RESIMG"  of="$OUT" bs=1M oflag=seek_bytes seek="$ROFF" conv=notrunc status=none
sync

echo "== verificerer det patchede image mod originalen =="
# Alle entry'er undtagen de fire patchede skal være byte-identiske med
# original-imaget; de patchede sammenlignes mod deres nye kilder.
# Hashing sker i bidder — entry'erne er op til ~1.5 GB, og læses de ind hele
# ad gangen (to kopier af rootfs samtidig) bliver processen OOM-dræbt.
python3 - "$OUT" "$ORIG" "$ROOTIMG" "$PARMBIN" "$BOOTIMG" "$RESIMG" <<'PYEOF'
import struct, sys, hashlib
img, orig, rootimg, parmbin, bootimg, resimg = sys.argv[1:7]
PATCHED = {'Image/rootfs.img': rootimg, 'Image/ramfs.img': bootimg,
           'Image/resource.img': resimg}

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
    if path in PATCHED:
        with open(PATCHED[path], 'rb') as r:
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

echo "== FÆRDIG: $OUT = original boot-kæde + Devuan-rootfs + eMMC-parameter + DTB uden uboot-logo =="
echo
echo "Flash boksen (den skal være i loader-tilstand, USB i OTG-porten):"
echo "  sudo $PROJ/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool uf $OUT"
echo "Tag derefter strømmen af/på — boksen booter Devuan direkte fra eMMC"
echo "(parameteren er bagt ind i imaget; hverken DI -p, SD-kort eller dd nødvendig)."
