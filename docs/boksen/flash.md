# Flash — sådan laver du en ny boks

## 3. Sådan laver du en ny boks

Fire trin. Alt andet er automatisk.

```bash
# 1. (kun hvis pakkelisten er ændret) hent pakkerne ind i bygge-rootfs'en
sudo devuan/extra_packages.sh

# 2. byg imaget og flash. Scriptet bygger FØRST (nogle minutter) og venter
#    derefter på ENTER — brug ventetiden på trin 3.
sudo devuan/testflash.sh

# 3. boksen i loader-tilstand: strøm fra → USB-kablet i boksens OTG-port →
#    hold update-knappen → strøm på → slip. Tryk så ENTER i scriptet.
#    Det tjekker selv at boksen er synlig på USB, og siger til hvis den ikke er.

# 4. tag strømmen af og på. Find boksen, og lav swapfilen:
devuan/find_box.sh
ssh -i ~/.ssh/geekbox_key root@<ip> 'bash -s' < devuan/emmc_first_boot.sh
```

Skærmen er sort de første 15-30 sekunder. Det er normalt — der er ikke noget boot-logo.

**Sådan ser en rigtig boot ud:** blåt LXDE-tapet, en grå bjælke i bunden med startmenu til
venstre og ur til højre. Firefox ligger i menuen og kan spille YouTube med lyd.

**Husk USB-donglen til musen.** Sidder den stadig i den forrige boks, ser det ud præcis som
en alvorlig softwarefejl. Vi faldt i.

---

## 4. Fase 1: Lubuntu på eMMC (flash)

**Virker:** `upgrade_tool` v1.23 (fra `geekboxzone/lollipop_RKTools`, branch `geekbox`):

```bash
sudo upgrade_tool uf Geekbox_Lubuntu_V160309/update.img
```

**Fejlede og hvorfor:**
- Moderne `vicharak-in/Linux_Upgrade_Tool`: parse fint, men **"Check Chip Fail"** —
  nyere værktøjer taler ikke RK3368's gamle maskrom-protokol
- Gamle `upgrade_tool`-kopier på nettet: segfault (dynamisk linket mod forældede
  biblioteker). v1.23 er statisk linket 32-bit og virker på Mint 22.3
- Update-knappen giver **Loader-tilstand**, ikke ægte Mask ROM (lsusb-teksten
  "in Mask ROM mode" er bare en statisk etiket fra usb.ids)

**v1.23's kommandoliste** (fra binærens usage): `UF DI EF RS WS RL WL EB` — ingen
`DB`/`RD`/`TD`. `RL/WL` svarede "not supported" i Loader-tilstand. Men **`DI -p`**
flasher parameteren — og forventer den som **ren tekstfil** (den binære PARM-fil
afvises med "parameter is invalid"). Gendannelse altid mulig: `UF` med update.img.

## 9. eMMC-migration (aug 2026)

Efter at systemet kørte stabilt fra SD, blev det flyttet til eMMC — SD-kortet er dermed
frit, og eMMC er markant hurtigere.

### Metoden (ingen reflashing nødvendig)

1. **Kopi:** eMMC's root-partition `/dev/mmcblk0p6` (label `linuxroot`, ~14 GB) mountes
   på den kørende Devuan, og hele rodfilsystemet rsync'es over:
   `rsync -aAXH --delete --exclude=/proc --exclude=/sys --exclude=/run --exclude=/tmp --exclude=/mnt --exclude=/swapfile / /mnt/`
   VIGTIGT: lav **ikke** mkfs på boksen — moderne e2fsprogs laver features 3.10 ikke kan
   læse. Den eksisterende fs (fra 2016-imaget) genbruges, `--delete` rydder op.
2. **Efter-ret:** fstab-label til `linuxroot`, swapfil genskabt (`dd` 2 GB).
3. **Parameter:** ny variant med `root=/dev/mmcblk0p6` (`devuan/parameter_emmc.txt`).

### Parameterens fysiske placering — og FTL-fælden

Ved skrivning fra den kørende boks (uden loader-tilstand) viste `/dev/mmcblk0` sector 0
sig at være **nuller** — rknand-FTL'en mapper blok-enheden med offset. Søgning med
`grep -abo PARM` fandt magien ved **0x400000** og igen for hver 0,5 MB — præcis de 8
redundante kopier (`PARAMETER_OFFSET=1024` sektorer) fra U-Boot-kilden. Kopi 1+2 var
byte-identiske med output fra `devuan/make_parm_bin.py` (DI -p skriver samme format),
så skrivningen blev: `dd if=parameter_emmc.bin of=/dev/mmcblk0 bs=1 seek=4194304 conv=notrunc`.
De resterende kopier fungerer som fallback, hvis kopi 1 skulle afvises — næsten
ubrickbar.

### Boot-hastighed: mål først, trim bagefter

**Værktøjerne:**
- `dmesg` — kernel/initramfs-timestamps (kernel færdig ~3s, "Freeing unused kernel memory")
- `/var/log/boot` — bootlogd logger rcS-scripts **med tidsstempler** (guld værd)
- `/var/log/Xorg.0.log` — X-starttidspunkt (uptime-baseret)
- egne epoch-markører i `myinit.sh` (`date +%s > /root/boottime.log` ved start og før
  `exec /sbin/init`) samt mini-scripts i `/etc/rcS.d/S01boottime` og `/etc/rc2.d/`

**Fund:** af ~47 sekunder til X stod **~31 sekunder** på én linje i `/var/log/boot`:
eth0's dhclient genforsøgte uden kabel (`No DHCPOFFERS received`) før wlan0 fik lov.
Resten var fine: kernel ~3s, initramfs ~9s, myinit 1s, rc2 ~3s.

**Fix:** carrier-guard i `/etc/network/interfaces`, så ifup af eth0 afbrydes øjeblikkeligt
uden link:
```
iface eth0 inet dhcp
    pre-up sh -c "grep -q 1 /sys/class/net/eth0/carrier"
```
Resultat: ~47s → ~15-18s. (Ligger også i script 01 til fremtidige kort.)

**Kosmetik:** mellem blåt boot-logo og desktop vistes logo-rester strakt/pixeleret
(fb'en genfortolkes ved overgangen). Fix: myinit nulstiller fb0
(`dd if=/dev/zero of=/dev/fb0`) → blå logo → sort → desktop.
```

## 10. eMMC-flash direkte fra PC — boks 2 (aug 2026)

Boks 2 blev flashet helt fra laptopen (i modsætning til boks 1's SD-omvej i §9).
Boksen var i loader-tilstand, og metoden er nu scriptet.

### Metoden i korte træk

`09_make_emmc_img.sh` bygger `devuan/update_devuan.img`: et ext4-image af
`devuan/rootfs` laves med PRÆCIS samme størrelse som originalens `Image/rootfs.img`
og dd'es ind på dens offset i en kopi af `update.img`, og vores egen parameter
(`root=/dev/mmcblk0p6` + `init=/root/myinit.sh`) skrives inden for parameter-
entryens eksisterende størrelse som binær PARM (via `make_parm_bin.py`, nul-paddet).
Headere, offsets og størrelser er dermed uændrede, og `UF` ser en struktur der er
byte-identisk med originalen (scriptet sha256-verificerer alle entry'er). ext4
laves med samme feature-liste som script 02 (`^64bit,^metadata_csum`!). Da `WL`
ikke virker i loader-tilstand på disse bokse, er `UF` af et sådant modificeret
image den eneste rene USB-vej til at skrive en hel rootfs-partition — og fordi
parameteren er bagt ind, er `UF` ogsá det ENESTE skridt: hverken DI -p, SD-kort
eller dd-omvej er nødvendig.

### Opskrift: ny boks fra laptop (komplet)

Forudsætninger: `devuan/rootfs` er bygget (01+06+07 kørt, plus evt.
`extra_packages.sh` for ekstra pakker som lxterminal), og boksen er i
loader-tilstand (USB i OTG; hold Update, tryk kort Reboot, slip Update).
SD-kort er **ikke** nødvendigt — behold boks 1's gamle kort som redningsmedie.

```bash
# 1+2. Byg og flash i én kommando. Scriptet bygger imaget (09), kontrollerer DTB'en,
#      og VENTER derefter på ENTER — så har du tid til at sætte boksen i loader-
#      tilstand først. Derefter flasher det (~5-10 min).
sudo devuan/testflash.sh

# 3. Tag strømmen af/på. Boksen booter Devuan direkte fra eMMC.
#    Bemærk: der er INTET blåt boot-logo mere hvis DTB_PATCH har været brugt —
#    skærmen er sort de første 15-30 sekunder, det er normalt.

# 4. Find boksen på nettet (dens MAC er tilfældig ved hver boot, så søg ikke på den)
devuan/find_box.sh

# 5. Efterbehandling: filsystem 1,4 GB -> 15 GB + 2 GB swapfil
ssh -i ~/.ssh/geekbox_key root@<IP> 'bash -s' < devuan/emmc_first_boot.sh

# 6. Til sidst: passwd for både kristian og root (07 sætter midlertidigt 'geekbox'),
#    og evt. wifi-credentials.
```

**Kontrollér at det lykkedes** (verificeret på boks 4, 19. aug 2026):

```bash
ssh -i ~/.ssh/geekbox_key root@<IP> '
  /usr/local/bin/fb_overscan.py --show   # skal vise et vindue, ikke "fuld skærm"
  id kristian | grep -o video            # skal findes, ellers fejler ovenstående tavst
  readlink /etc/alternatives/desktop-background   # skal pege på et tapet
  command -v sudo                        # skal findes
  date                                   # skal være rigtig (chrony)'
```

På skærmen: blåt LXDE-tapet og en synlig bjælke i bunden med startmenu og ur.

Fallback hvis UF mod forventning ikke får parameteren med (vi har kun set den
slags med `DI -p`, aldrig med `UF`): skriv den bagefter med §9's dd-metode fra
en kørende boks — se fælde 1.

### Fælder fundet undervejs (alle løst)

1. **DI -p af emmc-parameteren slog tilsyneladende ikke igennem** (uforklaret): UF ok +
   `DI -p parameter_emmc.txt` ok, men boksen fortsatte med den forrige parameter
   (originalen: `init=/sbin/init` uden myinit → det kendte rcS-hæng: blåt logo, intet
   netværk). Beviskæde: boksen nåede aldrig myinit (intet ARP-svar på daværende
   myinit's statiske 192.168.1.50 trods link), mens en SENERE `DI -p` med
   SD-parameteren fra ren loader-tilstand virkede fint. Første DI -p kørte lige efter
   UF's auto-reboot — muligvis var boksen ikke i reel loader-tilstand trods "ok".
   Løsning: fra en kørende boks (bootet fra SD) skrives parameteren med §9's
   dd-metode: `dd if=parameter_emmc.bin of=/dev/mmcblk0 bs=1 seek=4194304 conv=notrunc`
   — verificeret ved readback + efterfølgende eMMC-boot. **Permanent fix bagefter:**
   09 bager nu parameteren direkte ind i imaget, så hverken DI -p eller dd indgår
   i opskriften længere — dd-metoden står som dokumenteret fallback.
2. **Forældet myinit i rootfs'en:** 06 kopierer `myinit.sh` ind i rootfs'en, men kopien
   var fra fejlsøgningsfasen: statisk IP 192.168.1.50, ingen fb-rettelser, og en
   blokerende `/usr/sbin/sshd -ddd`-linje til sidst (debug-sshd kører i forgrunden,
   så `exec /sbin/init` ville aldrig blive nået → netværk oppe, men aldrig nogen
   desktop). 09 kopierer nu altid den aktuelle `devuan/myinit.sh` ind umiddelbart før
   bygning. Samme rootfs manglede også **isc-dhcp-client og fbset** (bygget før de kom
   med i script 01) — 09's preflight tjekker nu for dem og printer fix-kommandoen.
   Tip: skal en boks nås på den statiske fallback-IP, giv laptopen en sekundær adresse
   på samme link: `sudo ip addr add 192.168.1.100/24 dev <interface>`.
3. **Carrier-guard på nedfældet interface — eth0 kom aldrig op:** myinit og
   `interfaces` testede `/sys/class/net/eth0/carrier` — men på et interface der er DOWN
   læses carrier som 0/EINVAL, og da ingenting nåede at sætte eth0 op, kom den aldrig
   op → boksen helt uden netværk (bevist via `/root/bootlog.txt`: eth0 DOWN uden MAC).
   Ramte ikke boks 1 (anden PHY-timing; wifi-dækning). Fix i både myinit.sh og script
   01's interfaces-skabelon: `ip link set eth0 up` FØR carrier-testen + venteløkke (5×1s).
4. **fb-normalisering målte den forkerte størrelse:** bufferens byte-størrelse er ikke
   ground truth — på boks 2 set: bpp=32 men stride=3840 (=16-bit linjelængde) → kraftig
   pixelering. Ground truth er **stride**: `stride/xres` = bytes pr. pixel. myinit vælger
   nu 16/24-bit efter stride (den gamle kode faldt pga. en parse-bug tilfældigvis altid
   i 16-bit-grenen — derfor virkede boks 1). Bemærk: fbset manglede i den gamle rootfs
   (kom med i script 01 senere) — efterinstalleret på boksen.
5. **Truncerede bruger-configs efter hårde strøm-cyklinger under hængende boots:**
   `~/.config/openbox/lxde-rc.xml` (0 bytes → openbox XML-fejl-popup ved login) og
   `~/.config/lxsession/LXDE/autostart` (0 bytes → kun openbox startede: sort skærm
   uden panel/desktop). Fix: kopiér system-defaults ind
   (`/etc/xdg/openbox/LXDE/rc.xml` og `/etc/xdg/lxsession/LXDE/autostart`, chown 1000).
   Lektie: sluk ikke boksen midt i første desktop-login — og tjek for 0-byte-filer i
   `~/.config` ved mærkelig desktop-adfærd (`find ~/.config -size 0`).
6. **ssh fra PC'en:** nøglen hedder `~/.ssh/geekbox_key` (ikke et standard-navn) →
   `ssh -i ~/.ssh/geekbox_key root@<ip>`.
7. **09's verifikations-script OOM-dræbt (RAM-travlt PC, aug 2026):** sha256-kontrollen
   læste hele entry'er ind i RAM ad gangen — ved `Image/rootfs.img` to kopier á ~1,4 GB
   samtidig (imaget + referencefilen) → kernelens OOM-killer dræbte python midt i
   kontrollen. Vigtigt at vide: imaget var alligevel komplet og brugbart — verifikationen
   kører EFTER dd, parm-bagning og sync, så et drab her betyder kun at kontrollen ikke
   nåede at bekræfte, ikke at imaget er defekt. Et allerede-bygget image kan derfor nøjes
   med at blive efter-verificeret (samme python-logik, standalone) uden genbygning.
   Fix: hashing i 8 MiB-bidder, konstant lavt hukommelsesforbrug.

### Efter første eMMC-boot (boks 2+3, over ssh)

Kør `devuan/emmc_first_boot.sh` på boksen (pipes ind over ssh, kører som root,
idempotent) — den udfører begge efter-trin:

```bash
ssh -i ~/.ssh/geekbox_key root@<IP> 'bash -s' < devuan/emmc_first_boot.sh
```

1. `resize2fs /dev/mmcblk0p6` — rootfs 1,4 GB → 15 GB (online, på mountet root)
2. swapfil 2 GB (`dd`+`mkswap`+fstab-linje) + `swapon -a`

Swapfilen kan ikke bages ind i `update_devuan.img`: 09 bygger rootfs-imaget med
præcis samme størrelse som originalens `Image/rootfs.img` (~1,4 GB), og
filsystemet er kun 1,4 GB stort indtil resize2fs efter første boot — en 2 GB
swapfil kan hverken ligge i imaget eller oprettes før udvidelsen. Uden swap
crasher boksen under tunge apps (firefox), så trinnet er ikke valgfrit.

- **Hvis første boot efter flash kun viser sort skærm med musmarkør:** løst aug 2026, og
  årsagen var to trivielle ting oven i hinanden — ikke displayet, som hele tiden tegnede
  et korrekt 1920x1080-billede. (1) Skrivebordsbaggrunden manglede
  (`/etc/alternatives/desktop-background` ejes af `desktop-base`, som vi ikke
  installerer) → sort skrivebord. (2) TV'et beskærer ~2,3 % på alle fire kanter, så
  lxpanel på 26 px i bunden forsvandt helt. Fix i både 07 og 09: tapet-symlink +
  `devuan/fb_overscan.py`, der skrumper billedet til 95 % centreret via fb-var'ens
  `grayscale`/`nonstd` (kernens egen `rk_fb_disp_scale()` er død kode) og hænges op i
  lxsession-autostart. Vigtigste fælde undervejs: `/dev/mem` på `fb0/phys_addr` rammer
  ikke framebufferen — adressen er en IOVA, fordi VOP'ens IOMMU er slået til. Fuld
  beviskæde og fældeliste: **docs/boksen/skaerm.md**.

### Status boks 2 (aug 2026)

Booter Devuan fra eMMC: netværk via DHCP, desktop med korrekte farver og stabilt
billede. Det observerede "blinken" (panel→sort→panel i loop) var den fysiske
HDMI-forbindelse: kernellen registrerede ingen gentagne HDMI-hændelser, kun én
EDID-læsefejl ved boot — signalet droppede på vej til TV'et. Løst ved at skifte
HDMI-stik/ledning. Ikke software.

### Status boks 3 (aug 2026)

Første boks flashet med den SD-frie metode (09 med bagt parameter + `UF` — intet
andre skridt). Virkede med det samme: boot fra eMMC, netværk via DHCP, desktop med
korrekte farver efter én X-genstart ved første boot (se ovenfor). resize2fs +
swapfil gjort over ssh. Metoden er dermed verificeret på hardware.
