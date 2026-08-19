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

# Udvid rodfilsystemet til hele partitionen, hvis det ikke allerede fylder den.
# HVORFOR HER: imagets filsystem er kun ~1,4 GB (09 bygger det i samme størrelse som
# originalens rootfs.img), mens partitionen er ~15 GB. Udvidelsen har hidtil været et
# manuelt trin (emmc_first_boot.sh) — og et manuelt trin bliver glemt. Konsekvensen er
# ubehagelig: browser-cache fylder de sidste MB på en aften, og en FULD DISK dræber
# X-sessionen TAVST. Ingen fejl i Xorg.0.log, tom .xsession-errors, ingenting i loggene
# (skrivninger fejler ju). Det ser ud som en helt anden fejl og kostede en aftens
# fejlsøgning 18-19/8-2026. Se DOKUMENTATION.md §10.
# Her, som PID 1 før init, skriver ingen andre processer på disken — det bedste tidspunkt.
# Stateless: vi sammenligner størrelser hver boot i stedet for at føre en markør-fil, så
# det også retter sig selv hvis partitionen en dag bliver større.
# NB: /proc/mounts har TO poster for "/" på denne boks — først initramfs'ens egen
# "rootfs", derefter den rigtige /dev/mmcblk0p6. Tag den SIDSTE der er en /dev-enhed,
# ellers får man "rootfs" og guarden springer alt over (målt 19/8-2026).
rootdev=$(awk '$2 == "/" && $1 ~ /^\/dev\// { d = $1 } END { print d }' /proc/mounts)
if [ -b "$rootdev" ] && [ -r "/sys/class/block/${rootdev#/dev/}/size" ]; then
    partsec=$(cat "/sys/class/block/${rootdev#/dev/}/size")           # 512-byte sektorer
    fsinfo=$(dumpe2fs -h "$rootdev" 2>/dev/null)
    fsblocks=$(echo "$fsinfo" | awk -F: '/^Block count:/  { print $2+0 }')
    fsbs=$(echo "$fsinfo"     | awk -F: '/^Block size:/   { print $2+0 }')
    if [ "${fsblocks:-0}" -gt 0 ] && [ "${fsbs:-0}" -ge 512 ] && [ "${partsec:-0}" -gt 0 ]; then
        fssec=$(( fsblocks * (fsbs / 512) ))
        # udvid kun ved reel forskel (>5 %), så vi ikke kalder resize2fs unødigt hver boot
        if [ "$fssec" -lt $(( partsec / 100 * 95 )) ]; then
            echo "myinit: udvider $rootdev: $(( fssec / 2048 )) MiB -> $(( partsec / 2048 )) MiB"
            resize2fs "$rootdev" > /root/resize.log 2>&1 \
                || echo "myinit: resize2fs fejlede, se /root/resize.log"
        fi
    fi
fi

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

# Swapfil på 2 GB, hvis der ikke er nogen. LIGGER HER, EFTER dropbear, med vilje: dd'en
# tager 1-2 minutter på eMMC ved første boot, og i den tid skal ssh være tilgængelig.
# HVORFOR AUTOMATISK: boksen har 2 GB RAM. Firefox med YouTube fylder det, og uden swap
# mislykkes en hukommelsesanmodning. Firefox lukker så selv den ramte proces — fanebladet
# eller hele browseren dør, mens MASKINEN kører videre og stadig svarer på ssh. Der står
# derfor INTET i kernens log (ingen "Killed process"); beviset ligger i
# ~/.mozilla/firefox/*/minidumps/. Målt 19/8-2026. Det var hidtil et manuelt efter-trin
# (emmc_first_boot.sh), og det blev glemt, ligesom resize2fs blev det. Swapfilen kan ikke
# ligge i imaget: den er større end den ledige plads i det låste 1408 MiB-image.
# Kører kun når der reelt mangler swap, og kun hvis der er rigelig plads bagefter.
# NB: test IKKE med [ -s /proc/swaps ] — procfs rapporterer altid størrelse 0, så den test
# er altid sand. Kig på indholdet: uden aktiv swap er der kun overskriftslinjen.
if ! grep -q "^/" /proc/swaps 2>/dev/null; then
    ONSKET=$((2048 * 1024 * 1024))
    rm -f /swapfile.tmp                       # rester fra en afbrudt boot
    faktisk=$(stat -c %s /swapfile 2>/dev/null || echo 0)
    if [ "${faktisk:-0}" -ne "$ONSKET" ]; then
        # VIGTIGT: en HALV swapfil må aldrig blive stående. Tager man strømmen midt i
        # dd'en (og det gør man, for den kører netop ved første boot), efterlader den en
        # ufuldstændig fil UDEN swap-signatur. Tjekker man kun om filen findes, springer
        # næste boot oprettelsen over og swapon fejler tavst for evigt — målt på boks 9,
        # hvor der stod en 136 MiB rest. Derfor: sammenlign størrelsen, og byg i en
        # .tmp-fil der først omdøbes når mkswap er lykkedes.
        [ "${faktisk:-0}" -gt 0 ] && \
            echo "myinit: /swapfile er ufuldstændig ($(( faktisk / 1048576 )) MiB) — laver den forfra"
        rm -f /swapfile
        fri_kb=$(df -k / | awk 'NR==2 {print $4}')
        if [ "${fri_kb:-0}" -gt $((6 * 1024 * 1024)) ]; then     # kræv >6 GB fri
            # I BAGGRUNDEN, med vilje: dd'en tager 1-2 minutter, og kører den synkront her,
            # kommer skrivebordet først bagefter. Så sidder man foran en sort skærm og tror
            # boksen er død — og tager strømmen, præcis midt i arbejdet. Det skete på boks 9.
            # Baggrundsjobbet arves af init når vi exec'er, og swappen er klar kort efter
            # skrivebordet. Den atomiske .tmp-metode gør en afbrudt boot harmløs.
            echo "myinit: laver 2 GB swapfil i baggrunden (klar om 1-2 minutter)"
            (
                if dd if=/dev/zero of=/swapfile.tmp bs=1M count=2048 2>/dev/null &&
                   chmod 600 /swapfile.tmp && mkswap /swapfile.tmp >/dev/null 2>&1; then
                    mv /swapfile.tmp /swapfile
                    grep -q "^/swapfile " /etc/fstab 2>/dev/null || \
                        echo "/swapfile none swap sw 0 0" >> /etc/fstab
                    swapon /swapfile 2>/dev/null
                else
                    rm -f /swapfile.tmp
                fi
            ) >/root/swapfile.log 2>&1 &
        else
            echo "myinit: for lidt plads til swapfil ($(( fri_kb / 1024 )) MiB fri)"
        fi
    fi
    if [ -e /swapfile ]; then
        grep -q "^/swapfile " /etc/fstab 2>/dev/null || \
            echo "/swapfile none swap sw 0 0" >> /etc/fstab
        swapon /swapfile 2>/dev/null
    fi
fi

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
