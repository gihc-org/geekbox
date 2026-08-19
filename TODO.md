# TODO: Nyere Linux på GeekBox (RK3368)

## Spor A (aktivt): Devuan Excalibur med vendor-kernel — BOOTER ✅ (aug 2026)

Moderne userspace (Devuan Excalibur/Trixie-base, armhf) på vendor-kernen 3.10.79.
Bevarer HDMI/GPU/WiFi. Boot-strategi: uændret boot-kæde på eMMC; root på eMMC
(begge bokse) — parameter med `root=/dev/mmcblk0p6` + `init=/root/myinit.sh`.
Ny boks flashes direkte fra laptop i loader-tilstand: `09` bygger en modificeret
update.img med Devuan-rootfs OG eMMC-parameter bagt ind (in-place patch,
verificeret) — derefter er `UF` det eneste skridt; hverken SD-kort, DI -p eller
dd er nødvendig (se DOKUMENTATION.md §10). Scripts i `devuan/`:

- [x] Udpak update.img (rkfwtools) — analysér boot-flow: monolitisk kernel, initramfs mounter via LABEL
- [x] Verificér parameter-format mod vendor U-Boot-kilde (`lollipop_u-boot`): sector 0, PARM+crc32_rk
- [x] 01: debootstrap Devuan rootfs (kræver Devuans egen debootstrap pga. cron-daemon-common/systemd)
- [x] 02: skriv rootfs til SD — ext4 SKAL laves med `^64bit,^metadata_csum` (ellers "error loading journal" på 3.10)
- [x] 03: flash modificeret parameter via `DI -p` (tekstfil-format; 04 gendanner)
- [x] Bevis SD-vejen: Lubuntu rootfs fra SD booter fint (findmnt viser /dev/disk/by-label/sdrootfs1)
- [x] chroot-test: Excalibur-userspace kører på 3.10
- [x] OpenSSH 10 virker IKKE på 3.10 (seccomp-sandbox dræber preauth; syscall 397/403 mangler) → **dropbear** bruges i stedet (06)
- [x] Boot-hæng løst: 14.04-initramfs flytter ikke /proc,/sys,/dev ind i nyt root → sysvinit hænger i rcS. Løsning: **`myinit.sh` som PID1-shim** (init=/root/myinit.sh) der mounter selv, starter netværk+dropbear, logger til kortet, og exec'er /sbin/init
- [x] **Ren uovervåget boot til runlevel 2 med ssh verificeret** (aug 2026)

Videre (prioriteret rækkefølge, aftalt aug 2026):
- [x] Sikkerhed: ssh strammet (dropbear `-s` = kun nøgler), bruger `kristian` oprettet (sudo-gruppe, nøgle-login), OpenSSH-service disabled
- [ ] Skift kodeord: `passwd` (root) og `passwd kristian` på boksen — gøres af ejeren selv
- [x] WiFi: VIRKER (aug 2026) — nl80211 + wpa_supplicant, wlan0 får DHCP ved boot via /etc/network/interfaces. Bemærk: `wext` virker ikke på denne bcmdhd, brug `nl80211`. Kræver `isc-dhcp-client` + `wireless-tools` (installeret på boksen)
- [ ] NetworkManager til wifi: script `devuan/08_network_manager.sh` skrevet (aug 2026) — NM + nm-applet styrer wlan0 (nye netværk vælges i LXDE-bakken eller med `nmtui`), eth0 bliver på ifupdown/myinit så ssh-debugstien er uændret. Scriptet migrerer kendte netværk fra wpa_supplicant.conf til NM-nøglefiler, så boksen ikke falder af nettet ved skiftet. **Afventer test på boksen** — kør `sudo devuan/08_network_manager.sh /dev/sdX1` med kortet i læseren
- [x] Grafisk miljø: VIRKER (aug 2026) — X + LXDE via **fbdev** med **nodm** autologin som `kristian`. Vigtige fælder løst: (1) fb0 rapporterer tilfældig bpp ift. reel buffer (EDID-race) → myinit normaliserer med fbset + vælger DefaultDepth 24/16 efter målt bufferstørrelse; (2) `xserver-xorg-legacy` + `allowed_users=anybody` kræves da der ikke er KMS; (3) bruger skal være i `input`-gruppen for at X kan åbne /dev/input/event*; (4) lightdm erstattet af nodm (lightdm's logind-seat-detektion virkede ikke her); (5) `systemd-sysusers` fejler på 3.10 (EINVAL på lock) → divert'ed væk så postinsts bruger adduser-stien; (6) adwaita-icon-theme .deb kunne ikke xz-dekomprimeres på boksen → ompakket til gzip på PC'en
- [x] Desktop: VIRKER (X+LXDE via fbdev, nodm autologin, mus/tastatur)
- [x] Lyd: VIRKER (aug 2026) — HDMI-lyd via YouTube verificeret. Tre lag af problemer: (1) trixies libasound2t64 bruger 64-bit-time ioctls som 3.10 ikke kender (ENOTTY ved open) → løst med **libasound2 fra Devuan daedalus** (32-bit time) i `/opt/alsa-da` + `LD_LIBRARY_PATH` via `/etc/profile.d/alsa-legacy.sh`; (2) vendor-driverens almindelige write-sti er i stykker (hw_ptr=0) — kun **mmap via dmix** virker → vendor's `/etc/asound.conf` (fra vendor_root) giver default-enheden `dmixer`; (3) PA's udev-detect lavede direkte hw-sinks (tavse) → `/etc/pulse/default.pa` bruger nu eksplicit `load-module module-alsa-sink device=dmixer`. NB: disse ændringer ligger på kortet, ikke i byggescripts — dokumenteres hvis kort genbygges
- [x] Boks 2 på eMMC (aug 2026) — flashet direkte fra laptop: `09` (in-place patch af update.img) + `UF`, parameter via dd-metoden. Fælder løst undervejs: forældet myinit i rootfs, carrier-guard på down-interface, fb stride/bpp-mismatch, truncerede lxde-configs efter hårde slukninger. Se DOK §10. Øvrige bokse: samme fremgangsmåde
- [x] lxterminal som standard i eMMC-imaget (aug 2026) — `devuan/extra_packages.sh` samler ekstra rootfs-pakker i en EXTRA_PACKAGES-liste (pt. lxterminal, locales, console-setup, chrony), bevidst uden om 07 der kun er desktop+lyd. 09's preflight-vagt stopper bygningen hvis pakkerne mangler i rootfs'en
- [x] Dansk tastatur helt (æ/ø/å) + dansk tidszone i eMMC-imaget (aug 2026) — æ/ø/å blev slugt ved indtastning i X fordi rootfs'en manglede et UTF-8-locale (den er bygget før 01 fik locales-pakken, så 01's locale-blok aldrig kørte her): i C-localet kan XLookupString ikke konvertere ikke-ASCII-keysyms. Fix: `locales`+`console-setup` (sidstnævnte giver også dansk tastatur på tty1-6; /etc/default/keyboard dækker kun X) via extra_packages.sh, som nu også genererer da_DK.UTF-8 og skriver /etc/default/locale (nodm's pam_env læser den, så LXDE-sessionen får LANG). Tidszone: /etc/localtime→Europe/Copenhagen + /etc/timezone. Verificeret på begge bokse: danske menuer, æøå kan tastes, LANG=da_DK.UTF-8 i sessionen
- [x] **Sort skærm efter flash (kun muse-markør, panelet væk) — LØST 18. aug 2026.** Årsagen var to trivielle ting oven i hinanden, ikke vendor-driveren: (1) skrivebordsbaggrunden manglede (`/etc/alternatives/desktop-background` ejes af desktop-base, som vi ikke installerer) → helt sort skrivebord; (2) **TV'et beskærer ~2,3 % på alle fire kanter** (~25 linjer top/bund, ~48 px i siderne) → lxpanel på 26 px nederst forsvandt helt, og menu-ikonet på x=0-40 med det. Displayet var aldrig i stykker: boksen tegnede et korrekt 1920x1080-billede helt ud i kanterne (verificeret ved at hente framebufferen ned som billede). Fix: tapet-symlink + `devuan/fb_overscan.py` (skrumper til 95 % centreret via fb-var'ens grayscale/nonstd, da kernens egen `rk_fb_disp_scale()` er død kode) hængt op i lxsession-autostart. Begge dele i 07 og 09. Verificeret på boks 3. NB: beskæringen sker **ikke** hver gang — fire boots samme aften: klippede/fin/klippede/fin, afhængigt af HDMI-forhandlingen. Det er hele forklaringen på "første boot sort, næste boot fin". NB2: brugeren skal være i gruppen `video`, ellers kan sessionens script ikke åbne `/dev/fb0` og fejler TAVST i autostart (rettet i 07 ved useradd + vagt i 09). **Alle fælder og blindgyder: DEBUG-SORT-SKAERM.md** — især at `/dev/mem` på `fb0/phys_addr` er en IOVA og ikke framebufferen, hvilket hele "kun de øverste 950 linjer"-teorien byggede på. Gammel erfaringsliste (stadig gyldig om DPMS og kernens timing-init):
  1. **DPMS-blank-stien er brød** (solid dokumentation: dmesg "hdmi remove from lcdc0" + "blank mode:4" præcis 10 min efter X-start; billedet vågnede ikke igen — kun hw-markøren tilbage). Fix: ServerFlags BlankTime/StandbyTime/SuspendTime/OffTime=0 i fbdev.conf (07 + imaget).
  2. **Rodårsag fundet (18. aug 2026): U-Boot-logo-overdragelsen i DTB'en.** Symptomet var, at kun de øverste ~950 linjer nåede skærmen (panelet i bunden væk) selvom fb-indholdet var komplet i RAM (`/dev/mem`) og X renderede fint (`xwd`) — praktisk taget altid på FØRSTE boot efter flash, og kureret af en strømcyklus. Forklaringen ligger i vendor-kilden: med `/fb/rockchip,uboot-logo-on = <1>` + `rockchip,disp-policy = <2>` (BOX_TEMP) kalder kernen **aldrig** `load_screen()` på den primære LCDC (rk3368_lcdc.c:2239) — den læser `screen->mode` ud af U-Boots registre (rk3368_lcdc.c:401-415) og reprogrammerer først timingen hvis HDMI'ens opløsning tilfældigvis afviger (rk_fb.c:3543-3561). Flaget ryddes kun af Androids `RK_FBIOSET_CONFIG_DONE`-ioctl (rk_fb.c:2856), som X/fbdev aldrig kalder → kernen stoler på loaderen for evigt. NB: målt på en dårlig boks er win0 selv helt korrekt (`x_act/y_act/dsp_x/dsp_y = 1920/1080/1920/1080`, 1:1) — afkortningen sidder i de timing-registre `disp_info` ikke viser, og som kun `load_screen()` skriver. Samme rodårsag forklarer at DPMS-blank ikke kan vågne igen, og fb'ens bpp/stride-inkonsistens som myinit lapper på. **Fix-forsøg 1 FEJLEDE: `uboot-logo-on = <0>` gør at boksen IKKE BOOTER** (målt 18/8: LED lilla, aldrig blå). Flaget styrer også U-Boots egen display-init via `board_fbt_preboot()`, hvis definition mangler i vores U-Boot-kildetræ — kunne ikke læses ud af kilden, måtte måles. Rollback: `patch_uboot_logo.py <image> --value 1` + reflash. **Fix-forsøg 2 (ikke afprøvet): `rockchip,disp-policy` 2 (BOX_TEMP) → 0 (SDK)** — gør rk_fb.c:3559's tredje betingelse sand, så `load_screen()` kører ved HDMI-connect, uden at røre loaderen. `devuan/patch_uboot_logo.py` (nu med `--prop`) + `09` med `DTB_PATCH=policy|logo|none`. Runtime-test af teorien uden reflash: skriv en ANDEN opløsning til `/sys/class/display/HDMI/mode` og tilbage igen. **Beviskæde, kildehenvisninger og diagnostik: DEBUG-SORT-SKAERM.md**
  3. ~~Manglende fontconfig-cache~~ var en FEJLSLUTNING som årsag til det sorte skærm (den nye boks havde varm cache i imaget og kom alligevel sort op). `fc-cache -f` i extra_packages.sh beholdes — det forkorter reelt første boot, men det var ikke synderen.
  4. dmesg-flood der ødelægger boot-beviser: 3.10-kernens compat-lag logger et komplet register-dump for HVERT kald til clock_gettime64 (armhf-syscall 403, som 3.10 ikke kender → ENOSYS) — glibc 2.41/t64 kalder det konstant fra alle 32-bit processer. Harmløst, men drukner alt andet i dmesg.
- [x] Desktop-baggrunden var sort efter flash — pcmanfm's wallpaper peger på /etc/alternatives/desktop-background, som desktop-base ikke er installeret til at eje. Fix: symlink til /usr/share/lxde/wallpapers/lxde_blue.jpg (07 + boksens eMMC)
- [x] Efter-flash-efterbehandling scriptet (aug 2026) — `devuan/emmc_first_boot.sh` (resize2fs + 2 GB swapfil; swapfilen kan ikke ligge i det faste image — se DOK §10 "Efter første eMMC-boot"). Samme session: 09's verifikation gjort OOM-sikker (hashing i 8 MiB-bidder) — se DOK §10 fælde 7
- [ ] **Beslutning: skal overscan-kompensationen køre altid?** Ligger nu i autostart med 95 % — panelet forsvinder aldrig, men prisen er permanent sort kant (~27 px top/bund, ~48 px i siderne) og let blødere skrift. Alternativ: tag linjen ud og kør `fb_overscan.py` manuelt på de ~50 % af boots hvor panelet mangler. Pænest af alt: slå overscan fra i TV'ets menu (Billedstørrelse → Skærmtilpasning, eller omdøb HDMI-indgangen til "PC") og drop kompensationen helt
- [x] **Boks 4 flashet og verificeret (19. aug 2026)** — første boot var rigtig: blåt tapet, synligt panel, overscan-kompensationen kørte af sig selv (`vindue 1824x1026 på 48,27`), chrony havde sat uret. Det krævede TRE rettelser, ikke én: tapet-symlink, `fb_overscan.py` i autostart, og brugeren i `video`-gruppen — uden den sidste fejlede kompensationen **tavst**. Se DEBUG-SORT-SKAERM.md §11 for kæden. Nye fund: `sudo` var slet ikke installeret (kun gruppen — nu i `extra_packages.sh` + vagt i 09), og boksen har ingen MAC i sin IDB, så den får en tilfældig ved hver boot (ny IP hver gang; brug `devuan/find_box.sh`)
- [x] **Mus/tastatur og lyd døde tavst — to udevd'er (19. aug 2026)** — initramfs'ens `udevd --resolve-names=never` overlever ind i rootfs'en, rcS starter endnu en, og udev-databasen bliver aldrig skrevet (`/run/udev/data` tom). Følge: X får INGEN input-enheder (kun linjen "The server relies on udev..." i Xorg.0.log, ingen fejl) OG PulseAudios ALSA-sink falder tilbage til `auto_null`, så lyden er væk. Fix: `pkill -9 udevd` i `myinit.sh` før `exec /sbin/init`. Se DOK §5.4b. Lydens **anden** halvdel: PA skal startes med daedalus-libasound — `Exec=env LD_LIBRARY_PATH=/opt/alsa-da/... start-pulseaudio-x11` i `/etc/xdg/autostart/pulseaudio.desktop`. NB: `/etc/profile.d` og `/etc/environment` rækker IKKE (kun login-shells / nodm's session får dem ikke), og en `Xsession.d`-snippet er direkte farlig: det gamle bibliotek mangler symboler nyere programmer kræver (`aplay`: `undefined symbol: snd_pcm_subformat_value`), så hele sessionen kan dø
- [x] **Automatisk udvidelse af rodfilsystemet (19. aug 2026)** — `myinit.sh` sammenligner nu filsystemets størrelse med partitionens og kalder `resize2fs` hvis der mangler over 5 %. Stateless (ingen markør-fil), og kører som PID 1 før init, hvor ingen andre skriver på disken. Baggrund: imaget har et 1,4 GB filsystem, browser-cache fyldte det på en aften, og en **fuld disk dræber X-sessionen TAVST** — tom `.xsession-errors`, ingen logs, ser ud som en helt anden fejl. Fælde fundet undervejs: `/proc/mounts` har to poster for `/` (initramfs' `rootfs` først, den rigtige `/dev/mmcblk0p6` bagefter) — tag den SIDSTE `/dev`-post, ellers springer guarden alt over. `emmc_first_boot.sh` beholdes til swapfilen. **`df` hører i de første tre kommandoer man kører, når noget uforklarligt går i stå**
- [x] **syslog-daemon i imaget (19. aug 2026)** — `sysklogd` i `extra_packages.sh` + vagt i 09. Uden den gik nodms fejlbeskeder i ingenting, og både den fulde disk og det døde udev var usynlige
- [x] **Hele kæden verificeret på en frisk flash (19. aug 2026, boks 6)** — ved FØRSTE boot, uden manuelle efter-trin: `resize2fs` kørte selv (`/root/resize.log`: "now 3799552 (4k) blocks long", 15 GB), præcis én udevd med 204 poster i databasen (mus + tastatur virker), rsyslog kører, tapet, overscan-kompensation, `sudo`, dansk locale og ur via chrony. Lyden manglede først, fordi PA's Exec-patch kun lå i `07`, som ikke var genkørt — **en rettelse i et script der ikke bliver kørt igen, er ikke en rettelse**. Derfor ligger den nu også i `09` med vagter, ligesom tapet-symlinket og `video`-gruppen. Regel fremover: alt hvad 07 sætter op, og som en boks ikke kan undvære, skal have et sikkerhedsnet i 09, fordi 09 altid kører før en flash
- [ ] Nye scripts fra aug 2026 der bør nævnes i DOK: `testflash.sh` (byg+flash med pause til loader-tilstand), `find_box.sh` (find boksen på nettet), `rollback.sh` (sæt uboot-logo-flaget tilbage og flash), `fb_overscan.py`, `patch_uboot_logo.py`
- [x] **Swapfil laves automatisk (19. aug 2026)** — `myinit.sh` laver en 2 GB swapfil ved første boot hvis der ikke er nogen, efter dropbear så ssh virker imens (dd'en tager 1-2 min). Baggrund: firefox med YouTube dør uden swap på 2 GB RAM — men **maskinen bliver ved at køre**, det er firefox' egen proces der lukkes, og der er INGEN "Killed process" i kernens log. Beviset ligger i `~/.mozilla/firefox/*/minidumps/*.dmp`. Fælde i fælden: test ikke med `[ -s /proc/swaps ]` — procfs rapporterer altid størrelse 0, så testen er altid sand. **`emmc_first_boot.sh` er dermed kun til ældre bokse**; nye får både resize og swap af sig selv
- [x] **rsyslog druknede i syscall-403-floden (19. aug 2026)** — 3.10 dumper alle registre ved hvert `clock_gettime64`-kald, hvilket blev ~30 MB/time skrevet til eMMC'en (syslog + kern.log) så snart rsyslog var installeret. Dumpet er KERN_WARNING, så prioritet kan ikke bruges. `09` lægger nu et indholdsfilter i `/etc/rsyslog.d/`. Målt: fra ~950 linjer/45 s til 0, mens `logger` stadig kommer igennem. NB: rsyslogs `regex` er POSIX BRE (`+` er et almindeligt tegn, `(a|b)` virker ikke) — brug `ereregex`
- [x] **Grafisk wifi (19. aug 2026)** — `network-manager` + `network-manager-gnome` i pakkelisten (16 MiB inkl. 16 afhængigheder). Den ikke-oplagte del er polkit: standardreglerne kræver en "aktiv session", og nodm laver ikke en, så uden en gruppebaseret regel kan man SE netværkene men ikke tilslutte sig. `09` sætter `netdev`-gruppen + `/etc/polkit-1/rules.d/50-nm-netdev.rules`. eth0 bliver bevidst `unmanaged` (ssh-vejen skal ikke afhænge af NM); wlan0 må ikke stå i `/etc/network/interfaces`, ellers lader NM den være. Verificeret: nm-applet i bakken, wifi-liste, tilsluttet. NB: er både boks og laptop på wifi, kan routerens client isolation blokere ssh mellem dem — brug kablet
- [x] **Firefox afbrydes med "stack smashing detected" (19. aug 2026) — dæmpet, ikke løst.** Ægte hukommelseskorruption (SIGABRT fra glibcs stak-canary), IKKE pladsmangel: swappen urørt, 1 GiB fri. Ramte både hovedprocessen og isolerede indholdsprocesser (kernen: `Comm: Isolated Web Co`, `potentially unexpected fatal signal 6`), på både youtube.com og dr.dk. **Sporet er lukket på denne boks:** vendor-kernen er bygget uden `CONFIG_COREDUMP` (`/proc/sys/kernel/core_pattern` findes ikke) og firefox' egen minidump-generering fejler — ingen af de to veje til et stakspor virker. Arbejdshypotese (ubevist): firefox' seccomp-sandkasse emulerer systemkald kernen mangler, og `clock_gettime64` kaldes tusindvis af gange i minuttet. Fire indstillinger i `/etc/firefox-esr/firefox-esr.js` (lagt ind af 09): `security.sandbox.content.level=0`, `fission.autostart=false`, `dom.ipc.processCount=1`, `browser.sessionstore.resume_from_crash=true`. Derefter 7 min YouTube uden nedbrud, og procestypen der crashede findes ikke længere. **Vi isolerede IKKE hvilken indstilling der var afgørende** — én test ville afgøre det: sæt sandkassen tilbage til 2 og se om nedbruddene vender tilbage; virker det, kan site-isolation tændes igen. Se HAANDBOG.md fælde 14
- [ ] Diagnostik til næste gang: `echo 1 > /proc/sys/debug/exception-trace` og `/proc/sys/kernel/print-fatal-signals` — uden dem er et nedbrud i en almindelig proces helt tavst i kernens log. Nulstilles ved boot; overvej at sætte dem fast i myinit
- [ ] `passwd` for kristian + root på alle bokse (07 sætter midlertidigt `geekbox`)
- [ ] Genkør `sudo devuan/extra_packages.sh` så `sudo` kommer i bygge-rootfs'en før næste image
- [ ] **eth0 får en ny tilfældig MAC ved hver boot** — der er ingen MAC i boksens IDB (`dmesg`: "Read the Ethernet MAC address from IDB:00:00:00:00:00:00"), så kernen genererer en. Følge: ny DHCP-IP ved hver boot, og routerens leasetabel fyldes op. Fix (lille): sæt en fast MAC i `myinit.sh` med `ip link set eth0 address ...` før dhclient — én pr. boks, fx afledt af boksnummeret. Værktøj til at finde boksen indtil da: `devuan/find_box.sh`
- [ ] Boks 1: opdatér `/root/myinit.sh` til repoets nuværende version ved lejlighed (`ssh -i ~/.ssh/geekbox_key root@<ip> 'cat > /root/myinit.sh' < devuan/myinit.sh`) — dens ældre myinit virker i dag, men kun fordi dens PHY-timing og EDID-race tilfældigvis opfører sig (se DOK §10, fælde 3+4)
- [x] RTC: løst (aug 2026) — chrony installeret; synker fra NTP i runlevel 2 når netværket er oppe. Boksen har ingen batteri-backup, så uret starter i 2013 ved hver boot indtil chrony retter det (apt virker herefter). NB: chrony manglede i eMMC-imaget (rootfs bygget før 01 fik den i pakkelisten; boks 1 fik den efterinstalleret) — nu i extra_packages.sh + vagt i 09. Efterinstalleres på boks 1 næste gang den er online: `apt-get install -y --no-install-recommends chrony`
- [ ] Desktop med GPU: vendor's libhybris-stak (armhf blobs i vendor_root/usr/local/lib) — research, lav prioritet (fbdev dækker det meste)
- [ ] **Kernel-sikkerhedsopdatering: 3.10.79 → 3.10.108** — se DRIVER-PORTERING.md §6.
      Baggrund: 3.10 er EOL siden nov 2017, men vi er tre år bagud *inden for* serien, og
      stable-reglerne forbyder ABI-ændringer i en stable-serie → driverne bygger nærmest
      uændret. Dette er et weekendprojekt, i modsætning til Spor B.
  - [ ] Hent vendor-kernekilden: `geekboxzone/lollipop_kernel` branch `geekbox`
        (mirror: `abhisit/rk3368-linux-3.10.79-lollipop-ubuntu`)
  - [ ] Reproducér den *nuværende* kerne først (samme `.config`, samme binær-adfærd) —
        uden en verificeret baseline er alt videre gætværk
  - [ ] Merge/rebase op til 3.10.108 — forvent konflikter i `mm/` og `arch/arm64/`,
        hvor Rockchip patchede kernens egne filer
  - [ ] Config-hærdning uden ABI-risiko: slå ubrugte protokoller/fs fra (DCCP, SCTP,
        AppleTalk, IPX, USB gadget), `CONFIG_MODULE_SIG`, `CONFIG_STRICT_DEVMEM`,
        `CONFIG_DEVKMEM=n`, Yama LSM, `kptr_restrict`, `dmesg_restrict`
  - [ ] Test på SD først; eMMC-Lubuntu forbliver fallback (script 04)
  - [ ] Bemærk: al moderne hardening (HARDENED_USERCOPY 4.8, FORTIFY_SOURCE 4.13,
        arm64 KASLR 4.6, STRICT_KERNEL_RWX ~4.11) findes ikke i 3.10 og kan ikke fås
- [ ] Overvej kabel frem for WiFi hvis boksen står utroværdigt: BCM4354-firmwaren er en
      upatchbar blob fra Broadpwn-æraen, der parser frames før authentication

## Spor B (parket indtil videre): Mainline kernel + nyere Linux på GeekBox (RK3368)

Mål: Erstatte vendor-kernen (3.10.79 fra 2015) med en mainline LTS-kernel og en moderne
arm64-userspace. Arbejdet sker på microSD-kort, så eMMC med den fungerende Lubuntu V160309
bevares som fallback, indtil vi ved, hvad der virker.

## Realistisk forventning (verificeret august 2026)

Mainline `rk3368-geekbox.dts` aktiverer i dag kun:
- ✅ eMMC, Ethernet (GMAC), USB host + OTG, UART2 (seriel konsol), watchdog, temperatursensor
- ❌ HDMI/display, GPU (PowerVR G6110 — ingen mainline-driver overhovedet), WiFi (AP6354), lyd

Konklusion: mainline-sporet giver en **headless server** (SSH, netværk, USB-storage).
En grafisk desktop kræver, at HDMI/VOP først får liv (stretch goal, se fase 7).
"Lubuntu" i praksis = moderne Debian/Ubuntu arm64-userspace; LXQt-desktop er afhængig af HDMI-resultatet.

## Fase 0 — Udstyr og sikring

- [ ] Anskaf USB-til-TTL 3.3V seriel adapter (CP2102/FTDI/CH340) — **uundværlig** til kernel-arbejde
- [ ] microSD-kort, 8 GB+ (helst hurtigt)
- [ ] Bekræft at gendannelsesvejen virker: `update.img` + `upgrade_tool` v1.23 ligger klar (se README.md) — boksen kan altid reddes via Mask ROM
- [ ] Find UART2-pins på boardet (TX/RX/GND på landing strippen — tjek geekbox.boards.net schematics via Wayback)
- [ ] Verificér seriel konsol på den *nuværende* Lubuntu først: vendor-kernen bruger typisk 1500000 baud, mainline dts bruger **115200n8**

## Fase 1 — Byggemiljø på laptopen (x86_64)

- [ ] `sudo apt install gcc-aarch64-linux-gnu u-boot-tools device-tree-compiler debootstrap qemu-user-static parted swig python3-dev libgnutls28-dev uuid-dev libssl-dev bc bison flex`
- [ ] Hent kilder:
  - [ ] U-Boot: `git clone https://source.denx.de/u-boot/u-boot` (har **geekbox_defconfig** — verificeret)
  - [ ] Kernel: seneste LTS fra kernel.org (`rk3368-geekbox.dts` er i mainline — verificeret)
  - [ ] rkbin: `git clone https://github.com/rockchip-linux/rkbin` — indeholder RK3368-blobs: `rk3368_ddr_600MHz_v2.06.bin`, `rk3368_miniloader_v2.68.bin`, `rk3368_bl31_v1.91.bin`, `rk3368_bl32_v0.10.bin`, `rk3368_usbplug_v2.68.bin` (alle verificeret til stede)

## Fase 2 — U-Boot på SD-kort

- [ ] Byg U-Boot: `make geekbox_defconfig && make CROSS_COMPILE=aarch64-linux-gnu-`
- [ ] Beslut boot-kæde: (a) vendor-kæde: rkbin miniloader + mainline U-Boot som bootloader, eller (b) fuld mainline: U-Boot TPL (DDR-init kræver rkbin ddr-blob) + SPL + U-Boot proper + bl31/bl32 fra rkbin
- [ ] Skriv til SD (u-boot `idbloader.img` på offset 32KB, `u-boot.itb` på offset 8MB — standard Rockchip-layout)
- [ ] Test på seriel konsol: U-Boot-prompt fremme? (Første milepæl — alt efter dette er "almindelig" kernel-arbejde)

## Fase 3 — Mainline kernel, minimal boot

- [ ] Byg LTS-kernel: `make defconfig` (arm64) + tjek at `ARCH_ROCKCHIP`/RK3368-relaterede drivers er med; byg `rk3368-geekbox.dtb`
- [ ] Minimal rootfs på SD-partition 2: Debian arm64 via `debootstrap --arch=arm64` (eller busybox til allerførste test)
- [ ] Boot argumenter: `console=ttyS2,115200 root=/dev/mmcblk1p2 rw` (verificér enhedsnavn — SD vs eMMC nummerering)
- [ ] Milepæl: login-prompt på seriel konsol fra mainline kernel

## Fase 4 — Netværk og brugbar server

- [ ] Ethernet op (GMAC er i dts) → DHCP → `apt` virker
- [ ] SSH-server installeret — herfra kan arbejdet ske uden seriel adapter
- [ ] Fyldende userspace: Debian stable arm64 (anbefalet — mindst besvær på gammel hardware), eller Ubuntu Server arm64
- [ ] Tjek eMMC-adgang fra mainline (`mmc0` er i dts) — forberedelse til fase 7

## Fase 5 — Desktop (kun hvis HDMI lykkes, se fase 7)

- [ ] `lubuntu-desktop` eller lettere: `lxqt`/`xfce4` oven på basen
- [ ] Forvent software-rendering (llvmpipe) — PowerVR G6110 har ingen mainline-driver; på 8×Cortex-A53 bliver det sløvt. Overvej om desktop overhovedet er formålet, eller om boksen er bedre som headless server

## Fase 6 — WiFi (stretch)

- [ ] AP6354 = Broadcom BCM4354-baseret, SDIO: kræver at sdio-controller aktiveres i dts + `brcmfmac` firmware og nvram — udtræk fra vendor-imagets `/system/etc/firmware` (update.img kan mountes/udpakkes)
- [ ] Bluetooth via `brcmfmac`/btbcm tilsvarende

## Fase 7 — HDMI (stretch, research-tungt)

- [ ] Undersøg mainline-status for RK3368 VOP + DW HDMI (`drivers/gpu/drm/rockchip`, `drivers/gpu/drm/bridge/synopsys/dw-hdmi.c`) — rk3368.dtsi har muligvis noderne, men GeekBox-dts aktiverer dem ikke; find ud af hvorfor (manglende driver? uprøvet?)
- [ ] Tilføj `&hdmi`/`&vop` (+ clock/phy-opsætning) til en lokal dts-overlay og test
- [ ] Fallback hvis desktop er et must: behold vendor-kernel og opgradér userspace forsigtigt (Bemærk: moderne systemd kræver kernel ≥ ~4.15; på 3.10 ender man med Debian jessie/stretch-æra userspace — begrænset værdi)

## Fase 8 — Skriv til eMMC (frivilligt, når SD-opsætningen er bevist)

- [ ] Kopier boot-kæde + rootfs fra SD til eMMC (dd / rkdeveloptool wl)
- [ ] Behold SD som redningsmedie

## Gendannelse (altid åben)

Mask ROM kan ikke overskrives. Uanset hvad der går galt: sæt boksen i Mask ROM-tilstand og flash
`update.img` igen med `upgrade_tool` v1.23 — se README.md, fremgangsmåde + fejlfinding.
