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
- [ ] **Kernel-rebuild-regression: WiFi mangler (26. aug 2026)** — vores genbyggede
  3.10-kerner (marts-defconfig) har `RTL8188EU=y`/`RKWIFI=y` men INGEN `bcmdhd` →
  wlan0 findes ikke, og NetworkManager har ingen wifi-mulighed (original-kernen
  har bcmdhd indbygget). Fix: næste kernel-byg med bcmdhd slået på; firmware ligger
  i /system/etc/firmware (kræver velfungerende system.img). Se
  `devuan/gpu/DDK15-BASELINE-FLASHTEST-SESSION-NOTAT-2026-08-26.md`.
- [ ] **NTP i stedet for HTTP-ur-sync (26. aug 2026, brugerønsket):** myinit's
  HTTP-Date-sync er arbejdsfixet; ønsket er at få chrony/NTP til at virke. Tidligere
  fejl: "No suitable source for synchronisation" selv med makestep — MISTANKE:
  samme syscall-403-problem (chrony er 32-bit armhf; brugte muligvis clock_gettime64
  der fejlede). Den nye kernel (compat-403-fix, build 26. aug ~17:5x) kan have
  fikset det — TEST chrony igen efter flash. Hvis det virker: fjern HTTP-fixet fra
  myinit (eller behold som fallback).
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
- [x] **GPU bragt til live (20. aug 2026)** — GLES 3.1 på PowerVR G6110 via hybris-stakken: vendors system.img loop-montet ved /system (myinit), logd+servicemanager+pvrsrvctl (`devuan/gpu/gpu_up.sh`), `cma=128M` i parameteren (ellers EPERM på 2. CMA-buffer), system()-shim mod glibc-2.41-EFAULT. Eget test-program renderer 500 frames (DOK §5.15, `devuan/gpu/test_triangle.cpp`). NB: X kan ikke køre samtidig med hwcomposer-præsentationen — testen kører med nodm stoppet
- [ ] **WebGL-frysen i spil (25. aug 2026, GL-layers-forsøg):** WebGL 2.0 + hele UI'et virker med Basic-kompositoren, men Subway Surfers (poki.com) frøs efter første frame (0 fps; main ~118 % CPU; content-clock_gettime-storm ~3,8 kHz; SoftwareVsyncThread-spin ~1,8 kHz; ingen GPU-proces). **`layers.acceleration.disabled=false` (GL-layers, webrender stadig false) får spillet til at animere i vinduet** (70–168k px/2–4 s; present #250+), og efter genstart virker den simple testside end-to-end (root 518k px + fb0 329k px/2 s) — **men skærmen opdaterer IKKE for spillet** (første frame når root, derefter 0; to svigtmønstre: GPU-proces-DeviceReset midt i kørslen ELLER blot sænket present-rate ~5 fps). To spil-kørsler tog boksen ned. **Stress-siden er bygget + kørt uden måle-læsninger (run C, 25. aug nat): GPU-proces DeviceReset WR_POST_UPDATE ~1 min inde; derefter renderer WebRender SORTE frames ind i EGL-overfladen — rAF/FPS/presents kører videre, vinduet er sort på skærmen, resten af skærmen har fine farver; gdb viser presents kører; xrefresh hjælper ikke; ingen CONTEXT_LOST. REVIDERET KONKLUSION: frysen = render-fejl efter GPU-genstart, IKKE present-/kompositeringsfejl; fb-driverens read-wedge (XGetImage/fb0 i D-state) er et separat måle-artefakt.** Run D (gfx:5): reset allerede ved present #2–3; MOZ_LOG=gfx:5 giver intet output. **Standalone GLES-loop UDFØRT (300 swaps uden fejl, 2,2 fps): vendor-stakken overlever — fejlen er Firefox/WebRender-samspillet; test_client-artefakt (fmt=4 RGB_565 vs hårdkodet RGBA8888-konvertering) skal rettes.** Run E+F (scale=1 og 0,5): begge overlevede 800+ presents / 0 reset → **reset'et er flaky (~50 % af kørslerne), ikke canvas-afhængigt**; Firefox-kompositor-kadence ~1,4 Hz uafhængig af canvas-størrelse (vsync-problematik). **PRIMÆR HYPOTESE (buffer-race):** Firefox laver 1×1→resize-dansen; `destroyBuffers()` sletter busy-buffere → use-after-free → sporadisk GL-fejl → WR_POST_UPDATE-reset; standalone-klienten laver ikke dansen og reseter aldrig. **Næste:** fiks buffer-lifecycle (retire, slet aldrig busy), genbyg eglplatform_x11.so, mål reset-raten over 4-6 kørsler → derefter swap/queue-fejllogning, kompositor-verifikation (WebRender vs. gammel GL-layers), resize-reproduktion, kadence (~1,4 Hz) → til sidst test spillet + andre WebGL-sider (Shadertoy, aquarium/three.js, Basemark sidst). Detaljer: DOK §5.15d, `devuan/gpu/GL-LAYERS-SESSION-NOTAT-2026-08-25.md`, værktøjer i `devuan/gpu/eglplatform_x11/` (webgl_stress.html, wedge_capture.sh, capture_stress.sh, stall_capture.sh, xdump.c, rootdiff.c, x32probe1-4.c, start_game.sh). **STATUS 23:45:** buffer-fixet (retired, slet aldrig busy, md5 409af875) er bygget + installeret, men reset'et skete stadig (run 1, 98 s/#100, 0 retire-hændelser) — hypotesen er ikke bekræftet; `gfx.webrender.force-disabled=true` ændrer intet (Firefox 128 har kun WebRender når acceleration er til — 25 WR*-tråde i GPU-processen). **`gl_reset_probe` UDFØRT (23:41): 300 frames 1280×720, glGetError/eglGetError pr. frame → 0 afvigelser** (bevis: `devuan/gpu/beviser/gl_reset_probe-2026-08-25.log`) — vendor-GL ren, reset'et er WebRender-intern. **Bugzilla-signaturmatch:** 1989579 (dup af 1986254 → 1667748) viser præcis vores sekvens (`WR_POST_UPDATE` efter `Failed to compile vertex shader` → `Handling webrender error 2`); desktop-rodårsag der var dma-buf-fd uden CLOEXEC arvet af child-processer. **RUN G (00:1x): RUST_LOG virker på ESR; ALLE shaders kompilerede Success (inkl. 1989579-shaderne), men reset kom alligevel ved #950 / ~11 min — UDEN shader-/GL-fejl og uden dmesg-signatur → shader-hypotesen AFKRÆFTET; reset kan komme sent; ~1,4 Hz er sandsynligvis transfer-cap ~2M px/s på fuld-vindue-presents.** **GENNEMBRUD 26. aug morgen — GPU-MMU-fault:** dmesg viser PVR-kernens `BIF0 - FAULT` (TPUA_USC læser fra umappet 0x0102FF9540) + `Innocent Lockup`-recovery → kontekst ødelagt → WR_POST_UPDATE. **`gfx.webrender.precache-shaders=true` gør shader-fejlen (`cs_border_segment` ved warmup) + reset DETERMINISTISK (målt 3×, #50–#150 / ~1–3 min); uden precache kompilerer shaderen Success og reset er flaky.** gdb fangede SIGSEGV i Renderer-tråden efter reset. **BUFFER-FIX INSTALLERET (01:1x, md5 4ba7a90d): `destroyBuffers()` flytter ALLE buffere til retired-kø (frigøres efter 3 presents) — PVR-MMU-faulten er VÆK (0 i dmesg), stress-kørslen stabil (#350–450 uden reset mod tidligere #2–#150 flaky; ét sent reset ved ~#450); FPS ~1,5 (2,7 kun med gdb-breakpoint på eglMakeCurrent — skjult flaskehals, ikke produktionsløsning).** **SUBWAY SURFERS FRYS FORKLARET (01:5x): spillets Unity-shaders kræver #extension GL_EXT_draw_buffers + GL_EXT_frag_depth, og driver-kompileren afviser begge — GL_EXT_draw_buffers står i GL_EXTENSIONS men kompilerer ikke (målt ES2+ES3 med shader_ext_test.c); GL_EXT_frag_depth mangler helt. Spillet kan derfor ikke rendere → frys. Dette er den egentlige spil-blokade, separat fra buffer/GPU-reset-sporet.** **WEBGL1-TVANG AFKRÆFTET (02:1x): webgl.enable-webgl2=false + driver-patch (GL_EXT_draw_buffers fjernet fra udvidelseslisten via bind-mount) — spillet fejler stadig: Unitys egne shaders kræver MRT-direktivet, og kompileren afviser det uanset udvidelseslisten (målt med trivial_test.c). Konklusion: MRT-blokaden er en 2016-æra driver-begrænsning, ikke konfigurationsfejl. Reverse-engineering/erstatning af bloben er urealistisk (lukket USC-firmware; flerårigt driverprojekt).** **Næste:** research nyere PowerVR-DDK til RK3368 (draw_buffers) som afgrænset spor, ELLER find spil uden MRT-shaders; buffer-fixet beholdes; dmesg-overvågning i start_game.sh. Detaljer: DOK §5.15e, `devuan/gpu/GPU-FAULT-GENNEMBRUD-SESSION-NOTAT-2026-08-26.md`, beviser i `devuan/gpu/beviser/`.**
  - **DDK-SPORET (26. aug 02:3x–04:0x): DDK 1.5@3830101 fundet (leddaz-dump-stash, Android 6.0.1) og PRØVEINSTALLERET med NEGATIVT resultat:** 1.5-userspace (32-bit) + Android 6.0-libc (`__register_atfork` mangler i 5.1-libc) hænger boksens indbyggede KM **hårdt ved EGL-init (2× wedge, anden gang på rent filsystem)**. **KM'en er bygget ind i kernen** ("Rogue L 0.22", tom /proc/modules, insmod "Invalid module format", rmmod "not supported") → 1.5-KM kræver kernel-rebuild, ikke .ko-bytte. Dumpets pvrsrvctl er 64-bit (ubrugbar på 32-bit-stakken). Restore til 1.4 gennemført og verificeret (baseline bekræftet); `/system`-image forstørret 192→254 MB (var 100 % fuldt). **Spor-videre (foreslået):** (a) 1.5-KM via kernel-rebuild (stort), (b) spil uden MRT-shaders, (c) accept. **LØSNINGEN ER DOKUMENTERET (26. aug ~04:2x):** `devuan/gpu/DDK15-KERNEL-REBUILD-LØSNING-2026-08-26.md` beskriver den fulde vej — 3.10-kernel-rebuild med 1.5-KM fra `geekboxzone/mmallow_kernel` (gren `geekbox`, `drivers/gpu/rogue`) + løsning af blokaderne (`__register_atfork` via shim/patch, 64-bit `pvrsrvctl` via dumpets 64-bit-runtime). **→ OVERTAGET: hele vejen er GENNEMFØRT + verificeret (næste entry).** Detaljer: `devuan/gpu/DDK-PROEVEINSTALLATION-SESSION-NOTAT-2026-08-26.md`, plan i `devuan/gpu/DDK-HANDOVER-2026-08-26.md`.**
  - **DDK 1.5 KØRER NU (26. aug ~15:3x, gennemført):** kernel-rebuild-vejen er
    FULDFØRT og verificeret på boks 1: genbygget 3.10-testkernel (PVR fra +
    TRACING + VT) booter; 1.5-KM `.ko` (Rogue_DDK_Android 1.5@3830101) insmod'et;
    1.5-UM (32-bit) + 64-bit pvrsrvctl + frisk 6.0-libc installeret;
    `pvrsrvctl-exit=0`; **`shader_ext_test` = draw_buffers OK (ES2+ES3)**; **Firefox
    WebGL = OK**; **GL_VERSION = "OpenGL ES 3.1 build 1.5@3830101"** (gl_version_probe).
    **MEN Subway Surfers fryser STADIG (26. aug ~16:0x):** spillet kræver også
    `GL_EXT_frag_depth` (ES2-probe: "Extension GL_EXT_frag_depth not supported" →
    WebGL context lost — bevis `/tmp/ff_poki.log`); 1.5's ES2-sti mangler både
    udvidelsen og `gl_FragDepthEXT` (kun ES3-kernens `gl_FragDepth` virker;
    `frag_depth_test.c`). → draw_buffers var nødvendigt men ikke nok.
    Restpunkter: (a) næste kernel-byg: `CONFIG_ANDROID_PARANOID_NETWORK` FRA
    (blokerede ikke-root-sockets; fixet på boksen med inet-gruppe 3003) + bcmdhd
    (wifi mangler); (b) bindapi-patchen er bind-mount og genkøres efter reboot;
    (c) chrony/NTP afvises på vores byg ("No suitable source") — arbejdsfix = HTTP-
    ur-sync i myinit.sh; (d) cs_blur-WR-shaderstøj i Firefox-loggen (blokerer ikke
    WebGL); (e) spil-blokaden: **BESLUTTET (26. aug): gralloc-lock-sporet
    forfølges** (præsentation i GPU-processen); Spor B/accept fravalgt. Detaljer:
    `devuan/gpu/DDK15-BASELINE-FLASHTEST-SESSION-NOTAT-2026-08-26.md`.
- [x] **Subway Surfers: 3D-scenen renderer cyan — LØST 8. sep 2026 (vnext12):**
  spillet er SPILBART (dreng, tog, bygninger; input virker). Rodårsag:
  (1) driverens depth-clear-værdi er ikke 1 selv efter glClearDepthf(1) fra
  spillet (1.5-quirk) → verdens-draws fejler LEQUAL mod ≈0-depth → proxy
  rydder depth ÉN gang pr. frame med EKSPLICIT rgl_glClearDepthf(1) før clear
  (env CYAN_DEPTHCLEAR=1); (2) UI-draws med depthtest=0+depthmask=1 skriver
  depth (depthmask-lap); (3) præsentationssymptom (spilområde forsvinder ved
  play) løst med alpha-shim/preload (alpha:true+premultipliedAlpha:true).
  Rest: blå glitches + lav fps (instrumentering/software-layers). Opskrift:
  `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md` (21:40-22:00).
  Historik + målinger: `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md`,
  `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md` (virkende konfiguration).
- [x] **Gralloc-lock-EINVAL LØST (26. aug):** /dev/sw_sync 0600 root:root →
  EACCES i lock'ens sync-fence-sti; chmod 666 (myinit-retry). x11ws usage
  0x80→0x3. Poki-load-crash LØST: glesv2-proxyen brød WebGL1/ES1 → behold
  original libGLESv2.
- [ ] **Sorte Firefox-chrome (26. aug aften, løst med software-layers):** med
  `layers.acceleration.disabled=true` vises chrome + side normalt (bekræftet).
- **God start i en ny session (cyan-scene: divisor ude, live-vs-offline, 8. sep 2026):**
  *"Læs `OVERBLIK.md`, `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md`
  (dagens målinger + beslutninger) og `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md`
  (virkende konfiguration + fælder). Boks 1 er GENSTARTET: IP 192.168.0.171
  (`bash devuan/find_box.sh`), kernel 3.10.0 clone3-fix kører; bring-up =
  `date -s @<laptop-epoch>` (uret står i 2013 efter reboot), insmod
  `/root/pvrsrvkm_leddaz.ko` + `sh /root/gpu_up.sh`, bindapi-lap
  (sed IP → `bash patch_android_bindapi.sh`), tjek `/dev/sw_sync` 0666.
  STATUS (alt målt 8. sep): drawBuffers=[COLOR_ATTACHMENT0] på fbo=3;
  glDrawElementsInstanced-stien virker offline; VBO/EBO-snapshots +
  matricer viser at store prog7-draws (q37/q38/q39…) ligger 100% i frustum;
  offline-replay af RIGTIGE data + spillets shaders rasteriserer (q38 7.218 px,
  q37 72.945 px); divisor=0 på alle live-attribs (efterladt divisor=1 giver
  0 px offline — men er IKKE live-årsagen); clearDepthf=1; live postdraw
  viser dog stadig ingen fragmenter fra verdens-draws (nonsky-indhold statisk
  43.556 px, grid uændret). live-vs-offline-modsigelsen er uløst. NÆSTE:
  (1) shadow-draw i proxyen — efter det rigtige instanced-draw køres et ekstra
  glDrawElements/glDrawArrays med samme tilstand og aflæses; svarer på om
  live-kontekstens draw-kald selv er brudt. (2) Præsentations-symptom: ved
  'Press to play'-start forsvinder spilområdet fra siden (2× målt) — kør med
  poki-fix-udvidelsen/alpha-shim indlæst. FÆLDER: load-stall ved 0% og 74%
  efter gentagne genstarter → RYD PROFILEN (cache2, startupCache, storage,
  cookies, sessionstore) før hver ny load; SIGKILL af Firefox giver beskidt
  profil → næste load fejler tidligt; proxy vnext2 (e4e4ca74, fuld-frame
  pre/post-diff) crashede 2/2 ved første store draw — brug vnext4
  (631f9a40, grid + nonsky-tælling + clearDepthf-log, stabil); hakkende lyd
  efter SIGKILL = hængt pulseaudio-strøm (`kill -9 <pulsepid>`);
  ssh-hang ved launch lige efter lang sleep — kør launch-kommandoen igen;
  /tmp på boksen ryddes ved reboot (byg probes om fra repo); ingen BiDi-reload
  under load (poki bot=1). Kode: `eglplatform_x11/egl_proxy.c` (vnext4-log),
  `scene_instanced_probe.c` (instanced + map-test), `scene_real_replay.c`
  (replay af fangede buffers; env REAL_DIVISOR til kollaps-test). Evidens på
  laptop: `/tmp/cyan_snap_20260908.tar`, `/tmp/cyan_draw_probe_20260908.log`.
  Proxy-backups på boksen: `/root/egl_proxy_*.so.bak`. Kernel-billede:
  `devuan/gpu/kernelbuild/out/clone3fix/ramfs-clone3fix.img`."*
- **God start i en ny session (cyan-scene + clone3-kernel, 6. sep 2026):**
  *"Læs `OVERBLIK.md` (overblik over alle elementer), derefter
  `devuan/gpu/CYAN-CLONE3-SESSION-NOTAT-2026-09-06.md` (seneste session med
  målinger + beslutninger) og `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md`
  (fuld virkende konfiguration + fælder). Boks 1: IP findes med
  `bash devuan/find_box.sh` (senest 192.168.0.142); kernel kører NU med
  clone3-fix (`3.10.0 #1 SMP PREEMPT Sun Sep 6 22:12:06`); bring-up = insmod
  `pvrsrvkm_leddaz.ko` + `sh gpu_up.sh` + bindapi-lap (sed IP → bash) + tjek
  `/dev/sw_sync` 0666. STATUS: scene-draws sker HELE TIDEN (500–13.000 verts
  pr. kald via glDrawElementsInstanced), men efter-draw-readback viser FBO'et
  ensartet himmel-cyan → store meshes skriver 0 pixels; poki-frit
  replay-probe (scene_replay_probe.c + /root/p7.vs|fs) beviser at spillets
  prog7-shaders + rasterisering VIRKER på 1.5. → fejlen ligger i draw-tilstand/
  data, ikke shader-pipeline. NÆSTE: (1) proxy `715716d0` er installeret og
  logger glDrawBuffers + GL_DRAW_BUFFER0..3 + postdraw — få én vellykket
  load+play og træk `/tmp/cyan_draw_probe.log`; afgør om scenen renderer til
  et andet attachment eller geometrien er uden for frustum. (2) Hvis
  drawBuffers ikke forklarer det: snapshot vertex-buffere via glBufferData/
  glBufferSubData-hook (glGetBufferSubData findes IKKE via eglGetProcAddress)
  og regn clip-space med de loggede matricer. FÆLDER: ingen BiDi-reload under
  load (poki bot=1 → fryser ved 0%); kølepause 5+ min + evt. ren profil ved
  load-stall; readPixels/toDataURL uden for frame er ubrugelig
  (preserveDrawingBuffer=false); proxy v3 med glReadPixels-hook korrelerede
  med load-stall (rul tilbage til v2-funktionalitet); Firefox crasher stadig
  sporadisk — tag målinger hurtigt. Kernel-billede:
  `devuan/gpu/kernelbuild/out/clone3fix/ramfs-clone3fix.img` (flashet;
  rollback: out/test-trace2/…-id.img)."*
-  **God start i en ny session (cyan-scene, 27. aug 2026):** *"Læs
  `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md` og
  `devuan/gpu/GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md` og fortsæt
  derfra. Subway Surfers loader stabilt og er spilbart (lyd/HUD) med
  konfigurationen i handoveren — men 3D-scenen renderer cyan (kun himlen;
  objekter usynlige). Målinger: canvas læser ensartet cyan, clear er pink,
  kun 18-verts-draws, rigtige teksturer uploades, 0 GL/JS-fejl. Find hvorfor
  scene-objekterne ikke tegnes på 1.5-stakken: log 18-verts-draw'ets
  shader/uniformer, tjek kapabilitets-check, sammenlign vertex-shader-
  matematik. Fælder og kommandoer i handoveren."*
- [ ] **Sorte Firefox-chrome (26. aug aften, bagefter konteksttabet):** med
  præsentationen virkende er det nu synligt at Firefox' toppanel (adressefelt,
  bogmærke-ikon, fanelinje) renderer SORT mens sideindholdet vises korrekt
  (hvid side / farverig Poki-side). Brugeren: "en mørk film hen over" — også
  over Poki-logoet øverst. Mistanke: accelereret/GL-lags-sti i Firefox-
  compositoren der fejler på 1.5-stakken (chrome tegnes separat fra
  sideindholdet). AFTALT: køres når konteksttabs-sporet er færdigt. Detaljer:
  `devuan/gpu/GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md`.
- [ ] **Python-frontend + GLES-daemon (i gang aug 2026)** — Python tegner UI på /dev/fb0 (skelet: `fb_overscan.py`), taler med en C-daemon over unix-socket (skelet: `devuan/gpu/test_triangle.cpp`), daemonen renderer GLES offscreen og blitter til skærmen. Operationskort + første skridt: `devuan/gpu/README.md`. Regler: X stoppet under brug; strøm-cyklus bagefter
  - **God start i en ny session:** *"Læs `devuan/gpu/README.md` og DOKUMENTATION.md §5.15. Vi skal i gang med Python-frontend + GLES-daemon-projektet — arkitekturen og de første skridt står i README'en og i dette TODO-punkt. Boksen findes med `devuan/find_box.sh`."*
  - **Plan (23. aug 2026): `devuan/gpu/GLES-DAEMON-PLAN.md`** — beslutning om cross-bygning (`g++-arm-linux-gnueabihf` mod `vendor_root`-libs), milestones M0-M4, testcyklus og fælder. Værktøjet er installeret; M0 (baseline-byg af `test_triangle` cross + verifikation på boks 1) er næste skridt.
  - **M0+M1 færdig (23. aug 2026):** cross-byg verificeret på boks 1; `gles_daemon.c` kører med ping/fb/scenes/render/clear/quit og blitter FBO til fb0 (fb0-dump verificeret). Vigtig opdagelse: hybris-wrapperens `_glReadPixels`-slot er NULL → SIGSEGV + exit(42); patchet via `hybris_dlsym` fra DDK'en (`patch_readpixels()`). Næste skridt: M2 `frontend.py`.
  - **M2 færdig (23. aug 2026):** `frontend.py` tegner UI på fb0 (baggrund, ramme, 5x7-tekst inkl. æøå), taler med daemonen og kører animeret fase-sweep — verificeret på boks 1 med fb0-dump + PNG-tjek (både med og uden `--no-gles`). Fælder løst: `line_length` på offset 44 (ARM-aligning) og `--rect`-split. Næste skridt: M3 (testcyklus-kørsel) og M4 (gpu_setup.sh-distribution).
  - **M2b færdig (24. aug 2026):** X-vindue-demo — `frame`-kommandoen returnerer rgba8/rgb565 pixels (ingen fb0-blit); `window_demo.py` (ctypes+libX11) kører 60 frames @ 10 fps på boks 1 med X kørende og lukker daemonen pænt. Fælder målt: `ZPixmap`=2 i `XCreateImage` (1 → SIGSEGV), openbox flytter vinduet, X maler ikke root-baggrund ved opstart, scp over kørende binary fejler. **Skærm-verifikation færdig:** vindue `0x1800001` IsViewable på `+640+357` midt i kørslen; to `fbdump` viser cos-mønster + hvid status-tekst præcis i vindue-området og 19.456 px forskel mellem dumpsene (fase kører). Nye fælder: daemonens start skifter aktiv VT (chvt 8 efter start), quit-dansen kunne efterlade daemonen og slå HDMI-displayet fra (sort skærm, X i live). **FIX (24. aug):** `daemon_exit()` med `_exit()` i gles_daemon.c springer over atexit-dansen — quit er nu sikkert, VT og HDMI-enable urørte (målt). Detaljer: `devuan/gpu/GLES-DAEMON-PLAN.md` M2b.
  - **M3 færdig (24. aug 2026):** testcyklus på boks 1 — `service nodm stop` → `gpu_up.sh` → daemon → `frontend.py --frames 30 --fps 5 --dump`; fb0-dump verificeret (UI: BG 0x10A3, ramme 0x7DFF, titel 0xEF9E; GLES-rect 102 unikke farver = cos-mønster); X startet igen via `service nodm start` (Xorg på vt7, aktiv VT matcher, lxpanel kører). Næste skridt: M4 (gpu_setup.sh-distribution). Detaljer: `devuan/gpu/GLES-DAEMON-PLAN.md` M3.
  - **NB — hovedmålet (bruger-præcisering 24. aug 2026):** test af GLES/hybris-stakken via et Python-oprettet X-vindue (M2b-vejen) — udført og verificeret (vindue → fb0, 10 fps, quit-sikker efter `_exit`-fix). M2/M3 (fb0-kiosk) er sekundær. Vindue-demoen er **BROWSER-VEJE eksperiment 3** (A-prototype) — præsentationstesen bekræftet. **A1-A5 målt (24. aug):** chromium 47 har både GLX- og EGL/GLES2-vej indbygget; kodi 15.2 linker direkte mod hybris libEGL/libGLESv2 (TARGET_HYBRIS); ws_module-kontrakten + PVR's statiske GL-extensioner kortlagt — detaljer i BROWSER-VEJE.md §3.A. **eglplatform_x11-prototypen er bygget og VERIFICERET (24. aug):** `devuan/gpu/eglplatform_x11/` — GLES 3.1 renderer offscreen og præsenterer i et X-vindue (XPutImage → fb0, ~9 fps @ 640x360, cos-scene). Fælder løst: hybris' EGL-init skifter aktiv VT væk fra X (platformen chvt'er tilbage ved første present), og tegning skal gå gennem vinduets egen X-forbindelse (klientens Display* som EGL-native-display). Næste skridt: test platformen med en rigtig app (fx Kodi) og evt. stock Firefox + MOZ_X11_EGL. Detaljer: `GLES-DAEMON-PLAN.md` M4a.
  - **Firefox-forsøg (24-25. aug 2026, BROWSER-VEJE eksperiment 2):** firefox-esr 140.12 med `MOZ_X11_EGL=1` + vores env loader vores libEGL + Android-EGL-kæden. **GLXTEST LØST (24. aug):** glxtest henter kerne-EGL-funktioner via `eglGetProcAddress` — platformens `ws_eglGetProcAddress` videresender nu kerne-EGL-navne til wrapperen (+ stubs), og glxtest-binæren er patchet (dybde-tjek 24→16; backup `/root/glxtest.orig`). **MØNSTER 0x300c/0x3000 LØST (24-25. aug):** Android-bindAPI/chooseConfig-patches (`patch_android_bindapi.sh`) + driverens minor2-bhi (`patch_driver_minor.sh`) + stub-`libGL` (fælde 24: Firefox' SymbolLoader tog Mesas gl*-symboler via dlsym FØR eglGetProcAddress) + platformens live-vinduesstørrelse (kompositorvinduet frøs på 1x1). **WebGL 2.0 målt virkende (25. aug, under strace):** `WEBGL_RESULT OK PowerVR Rogue G6200, or similar WebGL 2.0` med `x11ws: present #2 (1280x948)`. **ÅBEN:** channel-error-race — content-processen dør (exit 0) før første present i normale kørsler; under strace kommer alt igennem. Skærmen er desuden sort (brugerobs; fb0 har mørkt indhold — se session-notat §8). Detaljer og alle spor: `devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-25.md`, DOK §5.15c, BROWSER-VEJE eksperiment 2, HAANDBOG fælde 23+24.
  - **God start i en ny session (Firefox-WebGL, 24. aug 2026) — historisk, planen udført:** *"Læs `devuan/gpu/GLES-DAEMON-PLAN.md` M4a, `BROWSER-VEJE.md` §4 eksperiment 2 og `DOKUMENTATION.md` §5.15c. Vi skal have WebGL til at virke i firefox-esr på boks 1. Status: `eglplatform_x11` virker for egne GLES-programmer (GLES 3.1 → X-vindue → fb0, ~9 fps), men Firefox' GL-probe (`glxtest`) fejler: den loader vores libEGL + Android-EGL-kæden, og `eglGetDisplay` svarer EGL_BAD_DISPLAY (logd 'eglGetDisplay:218 error 300c') → 'libEGL no display' → fallback til Mesa-software. De samme kald virker i `dlopen_egl_test.cpp`/`egl_display_probe.cpp`. Åben hypotese: glxtest kalder `eglGetDisplay` med sit X-`Display*` (Android-loaderen afviser non-default med 300C). Plan: (1) find boksen med `devuan/find_box.sh`; (2) verificér stakken med `sh /root/gpu_up.sh` + `egl_display_probe` (env: `LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0`); (3) spor glxtest's `eglGetDisplay`-argument med gdb for at bekræfte Display*-hypotesen; (4) tving kaldet gennem hybris-wrapperen (shim-præcedens med RTLD_GLOBAL eller wrapper-patch); (5) når proben siger PowerVR/EGL: kør firefox-esr helt og verificér WebGL via about:support + platformens præsent-log + fbdump. Fælder: hybris' EGL-init skifter aktiv VT (chvt tilbage til X' VT — fælde 19), tegning skal gå gennem vinduets egen X-forbindelse (fælde 20), popen/pgrep fejler i hybris-processer (fælde 21), /proc/cmdline har NUL-argumenter (fælde 22), glxtest rammer Android-loaderens eglGetDisplay (fælde 23). Ryd op efter forsøg: `pkill -9 -f firefox`, tjek VT/HDMI."*
  - **God start i en ny session (WebRender-kontekst, 24. aug 2026):** *"Læs `devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md` (handover med alle spor og kommandoer), `BROWSER-VEJE.md` §4 eksperiment 2, `DOKUMENTATION.md` §5.15c og `HAANDBOG.md` fælde 23. Vi skal have WebGL til at virke i firefox-esr på boks 1 (192.168.0.188). Status: glxtest er GRØN — PowerVR Rogue G6110, GLES 3.1, TEST_TYPE=EGL — efter platformens `ws_eglGetProcAddress` videresender kerne-EGL-navne til wrapperen (glxtest henter dem via `eglGetProcAddress`, ikke dlsym) + stubs, og glxtest-binæren er patchet (dybde-tjek 24→16, backup `/root/glxtest.orig`). Fuld Firefox blokerer stadig på WebRender-hardwarekontekst → 'Fallback WR to SW-WR'. To målte fejlmønstre (gdb): 0x300c — `eglBindAPI(ES)` lykkes, men `eglCreateContext` rammer IKKE wrapperen (Android-intern via ikke-kortlagt vej, libepoxy mistænkt); 0x3000 — wrapperens `eglCreateContext` + `eglMakeCurrent` på pbuffer virker, men kontekst-`Init` fejler bagefter. Wrapperens `eglCreateWindowSurface` ramte aldrig (0 hits) — Firefox starter offscreen/pbuffer. Plan: (1) find boksen med `devuan/find_box.sh`; (2) verificér stakken (`sh /root/gpu_up.sh` + `egl_display_probe`, env: `LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0`); (3) byg og kør epoxy-mimic'en (kilde `/tmp/epoxy_mimic.c`, byg på boksen med `-I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm`) — tester libepoxy's EGL-dispatch, som Firefox/libxul linker mod; (4) find mønster-A-kaldets funktionspointer: gdb med break på wrapperens `eglGetProcAddress` (log navne) og evt. på Android-intern `eglCreateContext` (adresse = `/system/lib/libEGL.so`-base + 0x6534, base fra `/proc/<pid>/maps`); (5) diagnosticér mønster-B-`Init` med `MOZ_LOG='GLContext:5'` (kig på `GLContext::InitImpl`); (6) når konteksten virker: kør firefox-esr helt og verificér WebGL via about:support + platformens præsent-log (`x11ws: vindue pakket ind` / `present`) + fbdump. Fælder/noter: ryd op med `pkill -9 -x firefox-esr` (ALDRIG `-f firefox` — dræber SSH-skallet), tjek VT (tty7) og HDMI-enable efter hvert forsøg; gdb: ingen `finish` i kommandoblokke, kun printf+continue; Android-loaderens libs (bionic) er usynlige for gdb; `dlsym_trace.so` er i stykker (brug ikke); en glibc-reinstall af firefox-esr fjerner glxtest-patchen (gen-anvend `/root/glxtest.orig` som reference)."*
  - **God start i en ny session (channel-error-racen, 25. aug 2026):** *"Læs `devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-25.md` (handover med alle spor), `DOKUMENTATION.md` §5.15c, `BROWSER-VEJE.md` §4 eksperiment 2 og `HAANDBOG.md` fælde 23+24. Vi skal have WebGL stabilt i firefox-esr på boks 1 (192.168.0.188). Status: ALT under stakken virker nu — GL 3.1 PowerVR Rogue G6110 i Firefox, WebRender-hardware uden SW-fallback, kompositor præsenterer 1280x948, og WebGL 2.0 er målt virkende (25. aug, under strace): `WEBGL_RESULT OK PowerVR Rogue G6200, or similar WebGL 2.0`. To nødvendige fixes sidder på boksen: stub-`libGL.so`/`libGL.so.1` i `/root/glstub/` (først i LD_LIBRARY_PATH — får Firefox' SymbolLoader til at falde tilbage på eglGetProcAddress i stedet for Mesas dlsym-symboler) og platformmodulet `/usr/local/lib/libhybris/eglplatform_x11.so` (live-vinduesstørrelse + op til 2 s ventetid ved surface-creation — kompositorvinduet frøs på 1x1). ÅBEN blokering: en channel-error-race — i normale kørsler dør content-processen (exit_group 0, ingen signalfejl) med 'Exiting due to channel error.' FØR første present, siden loader ikke og titlen forbliver 'Mozilla Firefox'; under `strace -f` kommer ALT igennem (timing). Desuden: skærmen er sort (brugerobs 24/25. aug) selvom fb0 har mørkt LXDE-indhold — separat display-pipeline-undersøgelse (session-notat §8). Plan: (1) bekræft nuværende tilstand med kommandoerne i session-notat §6 (kør evt. under strace for reference-beviset); (2) find hvem der printer 'Exiting due to channel error' (pid-tag loggen eller strace-tidslinjen) og hvorfor content-processen forsvinder (mistænkt: main-processen laver GPU-arbejde in-process og blokerer content-handshaket — prøv at få separat GPU-proces til at starte rent: `layers.gpu-process.enabled=true` er allerede sat i profilen, men `-gpuprocess` exec'es ikke; tjek GPU-proces-startfejl med vores env); (3) når content overlever: verificér WebGL via `WEBGL_RESULT` i loggen + vinduestitel + `x11ws: present` + fbdump (`cat /dev/fb0 > /root/ff_fb.raw` — NB read'en returnerer kun 2 073 600 bytes trods 16 bpp); (4) opdater `DOKUMENTATION.md` §5.15c, `BROWSER-VEJE.md` §4, `HAANDBOG.md` fælde 23/24 og session-notatet. Fælder: ryd `/root/ffprof/.parentlock` før hver kørsel (ellers Troubleshoot-dialog); `pkill -9 -x firefox-esr` (aldrig `-f`); tjek VT (tty7)/HDMI; gdb crasher på ARM ved LR-retur-breakpoints (brug `egl_trace_lib.c` i stedet); Firefox dlopen'er `libEGL.so` FØR `libEGL.so.1` og `dlsym(handle)` ser IKKE LD_PRELOAD-symboler."*
  - **God start i en ny session (M2b-verifikation, 24. aug 2026):** *"Læs `devuan/gpu/README.md` og `devuan/gpu/GLES-DAEMON-PLAN.md` (især M2b). Vi skal gøre M2b færdig: X-vindue-demoen (`window_demo.py` + daemonens `frame`-kommando, rgb565) er implementeret, kører 10 fps og er committed (`2219f6d`) — men den endelige skærm-verifikation mangler: vis at demo-vinduets indhold faktisk når `/dev/fb0`. Boksen findes med `devuan/find_box.sh`. Plan: kør `clearroot` først, start daemon + `window_demo.py` med X kørende, tjek vindue-mapping med `xwininfo -id` midt i kørslen, dump fb0 med `fbdump`. Husk fælderne i planen: ødelæg ikke root-børnevinduer (det slog lxpanel ihjel), tjek aktiv VT mod X' vt, og dræb efterladte daemoner med `pkill -9`."*
- [ ] Trin 2 (nyttiggørelse): vendor-kodi-binæren fra 2016 til lokal video (muligvis kørbar nu — brikkerne ligger i `vendor_root`), eller portér en moderne browser (uger-måneder: firefox/chromium kræver EGL-X11/DRI/KMS — løsningsanalyse, rækkefølge og billige eksperimenter: `BROWSER-VEJE.md`, aug 2026). **Alternativ til WebGL-spil:** `chromium` (150.x i armhf-arkivet; egen software-GL SwiftShader, forvent `--no-sandbox` og CPU-fart) — afprøv ved at lægge den i `extra_packages.sh` (installation i qemu-chroot, jf. DOK §5.9). NB (20. aug 2026): direkte installation på boksen brownout-crashede den — men det var strømforsyningen, ikke chromium (DOK §5.14). Med god strøm virker boks-installation; imaget kan dog stadig ikke rumme chromium (rootfs 84 % fuld med firefox)
- [ ] **Kernel-sikkerhedsopdatering: 3.10.79 → 3.10.108** — se DRIVER-PORTERING.md §6.
      Baggrund: 3.10 er EOL siden nov 2017, men vi er tre år bagud *inden for* serien, og
      stable-reglerne forbyder ABI-ændringer i en stable-serie → driverne bygger nærmest
      uændret. Dette er et weekendprojekt, i modsætning til Spor B.
  - [x] **Kernel-rebuild er også DDK 1.5-vejen:** 1.5-KM (fra `geekboxzone/mmallow_kernel`,
        gren `geekbox`) — **KORRIGERET 26. aug:** `drivers/gpu/rogue` = 1.4@3632228
        (ikke 1.5); 1.5@3830101 findes kun som præbygget .ko → byg 3.10-kernen UDEN
        indbygget PVR og insmod 1.5-.ko'en. Bootimg-fælde løst (SHA1-`id` i
        `package_bootimg.py`). **UDFØRT + VERIFICERET 26. aug (GL_VERSION
        "OpenGL ES 3.1 build 1.5@3830101", WebGL OK, draw_buffers OK).** Fuldt
        overblik + kommandoer:
        `devuan/gpu/DDK15-KERNEL-REBUILD-HANDOVER-2026-08-26.md` + løsningen i
        `devuan/gpu/DDK15-KERNEL-REBUILD-LØSNING-2026-08-26.md`
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
