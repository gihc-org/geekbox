# GeekBox (RK3368): Fra støvsamler til moderne Linux — den fulde historie

Dette dokument beskriver hele forløbet: hvordan 10 GeekBox-bokse fra 2015-16 fik nyt liv
med Devuan Excalibur (Debian 13-base), hvilke beslutninger der blev truffet undervejs,
hvad der fejlede, og hvorfor. Målet er, at læseren kan forklare løsningen videre — og
genskabe SD-kortet fra bunden.

---

## 1. Udgangspunktet

- **Hardware:** GeekBox, Rockchip RK3368 (8×Cortex-A53 arm64, 2 GB RAM, eMMC, microSD,
  HDMI, Ethernet, WiFi AP6354/BCM4354, GPU PowerVR G6110)
- **Problem:** producenten er væk, hjemmeside og fora døde, værktøjer fra 2015-16
- **Mål:** skrot Android, kør en så ny Linux som mulig

## 2. Den vigtigste beslutning: vendor-kernel + ny userspace

Der er to spor (se TODO.md), og valget faldt på **Spor A**:

- **Behold vendor-kernen (3.10.0)** — den har fungerende drivere til HDMI, GPU, WiFi, eMMC og SD.
  Til gengæld er den fra 2015 og mangler nyere syscalls.
- **Ny userspace på SD-kort** — Devuan Excalibur (armhf). Devuan fordi den ikke bruger
  systemd: moderne systemd kræver kernel ≥ ~4.15 (cgroup v2), og ville være en kamp mod
  kernen hele vejen. Devuan med sysvinit kører fint på 3.10.
- **eMMC røres næsten ikke** — kun én 580-bytes parameterfil ændres. Hele boot-kæden
  (loader, U-Boot, kernel, initramfs) bliver stående, og Lubuntu på eMMC er altid en
  fungerende fallback (script 04).

### 64-bit kernel, 32-bit brugerland — et bevidst valg

Værd at fremhæve, fordi det forklarer flere af kampene senere:

- **Kernen kører 64-bit** (`uname -m` → `aarch64`)
- **Hele brugerlandet kører 32-bit** (armhf) — både vendor-Lubuntu og vores Devuan

Det virker, fordi kernen er bygget med ARM32-kompatibilitetslag (`CONFIG_COMPAT`), der
lader 32-bit-binærer køre på 64-bit kernen. Vi valgte 32-bit, fordi vendor's
GPU/libhybris-blobs og hele 2016-brugerlandet er armhf — så kan de genbruges direkte
(fx lydens dmix-config). Bonus: Devuan Excalibur armhf har 64-bit `time_t` efter Debians
t64-transition, så den er y2038-sikker.

Prisen: 3.10's compat-lag kender ikke nyere syscalls (statx, clock_gettime64, OFD-locks)
og matcher ikke moderne ALSA-ioctl-struct-layouts. Det blev roden til OpenSSH-nedbruddet,
sysusers-låsefejlen og lyd-problemet — se §5.5, §5.9 og §5.11.

Alternativet (Spor B, mainline kernel) er parkeret: mainline understøtter hverken
HDMI, GPU eller WiFi på RK3368 — kun en headless server ville være realistisk.

At porte vendor-driverne til en mainline-kernel er ikke bare besværligt, men reelt
uoverkommeligt. Baggrunden — hvorfor kernens interne ABI ikke er stabil, og hvorfor
ndiswrapper-tricket ikke kan gentages Linux → Linux — står i
**[DRIVER-PORTERING.md](DRIVER-PORTERING.md)**. Samme dokument forklarer, hvorfor
Spor A's grænseflade (syscall-ABI'en) er den ene, der faktisk holder.

## 3. Boot-arkitekturen — nøglen til alt det andet

```
BootROM (i chippen, kan ikke slettes)
  → IDB-loader @ sector 0x40        (DDR-init + miniloader)
  → U-Boot @ sector 0x2000
  → parameter @ sector 0            (PARM-header + tekst + crc32_rk)
       indeholder CMDLINE med bl.a. root=..., mtdparts=...
  → U-Boot læser mtdparts og loader:
       resource.img @ 0x6000        (device-tree + logo)
       boot @ 0xE000                (Android bootimg: kernel + initramfs)
  → kernel 3.10 starter, initramfs mounter root ud fra root= i cmdline
  → /sbin/init på rootfs'en
```

Verificeret mod vendor U-Boot-kilden (`geekboxzone/lollipop_u-boot`):
- Parameter-format: `"PARM"` + u32 længde + tekst + **crc32_rk** (Rockchips egen
  CRC32-variant: MSB-first, poly 0x04C10DB7, init 0 — ikke standard CRC32!)
- U-Boot sammensætter selv cmdline: tilføjer `earlyprintk=...` foran og
  `storagemedia=emmc uboot_logo=... hdmi.vic=... androidboot.mode=emmc` bagefter
- Boot-konceptet med SD: root=LABEL=... resolves af initramfs til *enhver* enhed
  med det label — også en SD-partition. Det er hele trick'et.

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

## 5. Fase 2: Devuan på SD — alle fælderne, kronologisk

### 5.1 Udpakning af update.img
`rkfwtools` (github.com/iscle/rkfwtools) — patchet til at tage argv (main havde
hardkodede stier) + `sys/syslimits.h`→`limits.h` (macOS→Linux). Gav os: Loader.bin,
parameter, uboot.img, trust.img, resource.img (dtb), ramfs.img (kernel+initramfs),
rootfs.img (hele vendor-Lubuntu).

### 5.2 ext4 og kernel 3.10
Første SD skrevet med `mkfs.ext4` fra 2023 → boksen: **"error loading journal"**.
Årsag: e2fsprogs 1.47 slår `metadata_csum` og `64bit` til som standard — begge kræver
kernel ≥3.18. **Fælde i `-O`-syntaksen:** en positiv feature-liste *tilføjer* til
standarderne; man skal eksplicit skrive `^64bit,^metadata_csum`. (Script 02.)

### 5.3 udisks auto-mount
Skrivebordsmiljøet auto-mountede kortet under partitionering/skrivning. Løst med
afmount i scriptet + `-F` til mkfs.

### 5.4 Initramfs flytter ikke /proc, /sys, /dev
Med korrekt fs og parameter: stadig sort skærm, ingen boot. Bevist via ssh-indlogning
på Lubuntu-fra-SD (bisect-test med vendor-rootfs på SD — virkede!). Den gamle
14.04-initramfs mounter root, men flytter ikke de virtuelle filsystemer ind i det nye
root → sysvinit hænger tidligt i rcS (usynligt, da der ikke er framebuffer-konsol).
**Løsning: `myinit.sh` som PID1-shim** (`init=/root/myinit.sh` i cmdline) — 25 linjer
der mounter proc/sys/dev selv, starter netværk + ssh *før* init, logger til kortet, og
til sidst `exec /sbin/init`. Bonus: tidlig ssh-adgang ved alle fremtidige problemer.

### 5.4b To udevd'er slås — mus, tastatur og lyd dør tavst

Den gamle 14.04-initramfs starter sin egen `udevd` (`/sbin/udevd --daemon
--resolve-names=never`), og den overlever ind i vores rootfs. `rcS` starter derefter endnu
en. To daemoner om samme netlink-socket betyder at **udev-databasen aldrig bliver skrevet**:
`/run/udev/data` står tom, og `udevadm info` kan ikke slå nogen enhed op.

Følgerne ser ud som to helt andre fejl:

- **Mus og tastatur virker ikke.** X spørger udev om input-enheder og får ingenting.
  `Xorg.0.log` siger kun *"The server relies on udev to provide the list of input
  devices"* og tilføjer aldrig en enhed. Ingen fejlbesked, ingen (EE)-linje.
- **Lyden forsvinder.** PulseAudios ALSA-sink kan ikke finde lydkortet og falder tilbage
  til `module-null-sink` ("auto_null"), så alt spiller lydløst — også selvom
  `/etc/asound.conf` og `default.pa` er helt rigtige.

Diagnose (målt på boks 5, 19. aug 2026): `pgrep -a udevd` viste tre processer, heraf pid
162 med `--resolve-names=never` fra initramfs'en. Efter `pkill -9 udevd` + én ren daemon +
`udevadm trigger --action=add` gik databasen fra 0 til 206 poster, `ID_INPUT_MOUSE=1` kom
frem, X hotpluggede musen med det samme, og PA fik sin `alsa_output.dmixer`.

**Fix:** `myinit.sh` dræber initramfs-udevd før `exec /sbin/init`, så rcS starter præcis én.

### 5.5 OpenSSH 10 dræbes af sin egen sandbox
ssh-forbindelser lukkede med det samme. Debug-log viste: preauth-child **SIGABRT** lige
efter "attaching seccomp filter". dmesg viste `sshd: syscall 397` (statx, kræver 4.11)
og `syscall 403` (clock_gettime64, kræver 5.1). OpenSSH 10's seccomp-sandbox og moderne
glibc passer ikke til 3.10. **Løsning: dropbear** (ingen seccomp-sandbox) — virker
med det samme. (OpenSSH-pakken ligger stadig i rootfs, men servicen er disabled.)

### 5.6 Netværks-racen
Tilfældige netværksudfald sporet til to aktører der begge satte default-route:
myinit (statisk eth0) og ifupdowns dhclient (wlan0). Og værre: uden Ethernet-kabel
efterlod myinit en død default-route på eth0, der kvalte alt (også svar på wlan0-ping).
**Løsninger:** myinit konfigurerer kun eth0 ved fysisk link
(`cat /sys/class/net/eth0/carrier`); `timeout 15;` i `/etc/dhcp/dhclient.conf` så en
død eth0 ikke blokerer wlan0's tur i ifupdown.

### 5.7 `reboot` slukker
`reboot`-kommandoen på denne boks slukker i stedet for at genstarte (PMIC-adfærd).
Brug strøm-cykling.

### 5.8 Uret starter i 2013
Ingen RTC-backup → apt brokker sig ("Release file not valid yet"). **Løsning: chrony**
— synker i runlevel 2 når netværket er oppe.

### 5.9 Pakke-installation på boksen
- `systemd-sysusers` (Devuans standalone-variant) fejler med "Failed to take
  /etc/passwd lock: Invalid argument" — dens låsemekanisme kræver nyere kernel.
  **Løsning: `dpkg-divert`** binæren væk → postinst-scripts falder tilbage til
  `adduser`-stien (som virker).
- `adwaita-icon-theme_*.deb`: xz-dekomprimering fejlede *kun på boksen* (samme md5,
  fin på PC'en; andre xz-pakker virkede). Aldrig fuldt forklaret — løst ved at ompakke
  til gzip på PC'en (`dpkg-deb -Zgzip -b`).

### 5.10 Grafik: fbdev og EDID-racet
- Vendor-Lubuntu brugte **slet ikke GPU'en** til X — kun framebufferen (`fbdev_drv`).
  Det gør vejen nem: `xserver-xorg-video-fbdev` + LXDE.
- **fb0's bit-dybde flapper ved boot** (EDID-race på nogle TV'er): nogle gange
  allokeres 32-bit-buffer men rapporteres 16-bit (og omvendt) → dobbeltbillede/
  pixelrod. Diagnosticeret ved at dumpe `/dev/fb0` og analysere rå data (16 vs 32 bpp).
  **Løsning: myinit måler bufferens reelle størrelse ved hver boot** (2 MB = 16-bit,
  8 MB = 32-bit), tvinger `fbset` til at matche, og vælger `DefaultDepth` i
  xorg.conf.d/fbdev.conf derefter.
- X som ikke-root kræver KMS — findes ikke → `xserver-xorg-legacy` +
  `allowed_users=anybody`.
- Mus/tastatur: `/dev/input/event*` er root:input (660) → bruger skal i `input`-gruppen.
- lightdm nåede aldrig at starte X (logind-seat-detektion) → **nodm** med autologin.

### 5.11 Lyd: tre lag af problemer
1. **trixies `libasound2t64`** bruger 64-bit-time ioctls → 3.10 svarer ENOTTY ved åbning.
   **Løsning:** `libasound2` fra Devuan **daedalus** (32-bit time på armhf) lagt i
   `/opt/alsa-da`, aktiveret globalt via `/etc/profile.d/alsa-legacy.sh`
   (`LD_LIBRARY_PATH`).
2. **Driverens almindelige write-sti er død** (målt: `appl_ptr` tæller, `hw_ptr=0` —
   hardwaren consumede nul samples). Kun **mmap via dmix** virker — bevist ved A/B-test
   mod eMMC-Lubuntu (dens `speaker-test` via default-enheden spillede; direkte
   `hw:0,0` var tavs — samme adfærd dér!). **Løsning:** vendor's egen
   `/etc/asound.conf` (fra vendor_root) definerer `dmixer`; PA peger på den.
3. **PulseAudio's udev-detect** lavede direkte hw-sinks (tavse). **Løsning:**
   `/etc/pulse/default.pa` bruger eksplicit `load-module module-alsa-sink device=dmixer`.

### 5.12 NetworkManager til wifi
Wifi-credentials lå håndkodet i `/etc/wpa_supplicant/wpa_supplicant.conf` — besværligt
når boksen kommer på nye netværk. **Løsning: NetworkManager + nm-applet** (script 08,
qemu-chroot mod det færdige kort i læseren). NM styrer *kun* wlan0; eth0 bliver på
ifupdown + myinit's link-guard, så ssh-debugstien er uændret. Det kræver blot at
wlan0-stanzen fjernes fra `/etc/network/interfaces` (Debians default
`[ifupdown] managed=false` får NM til at lade filens interfaces være). 08 migrerer
også eksisterende netværk fra wpa_supplicant.conf til NM-nøglefiler i
`/etc/NetworkManager/system-connections/` — ellers ville boksen være faldet af
nettet ved skiftet. `kristian` er i `netdev`-gruppen + polkit-regel i
`/etc/polkit-1/rules.d/50-nm-netdev.rules`, så GUI'en kan ændre netværk uden en
logind-session (nodm opretter ingen). Nye netværk tilføjes via nm-applet i
LXDE-bakken eller `nmtui` i terminal (virker også over ssh).

### 5.13 WebGL: browseren er frisk — grafikstakken er lukket (aug 2026)

Webspil melder "browseren understøtter ikke WebGL" i firefox-esr. Det er hverken
browseren (140.12esr, fuld WebGL2-støtte — og `webgl.disabled` står ikke i
`firefox-esr.js`) eller manglende GL-biblioteker (Mesa 25.0.7 ER installeret som
afhængighed af firefox, med swrast/zink/kms_swrast). En GPU-stak er tre lag, og kun
det nederste findes på boksen:

1. **Kernel-driver (`pvrsrvkm`)** — loadet: `/dev/pvrsrvkm` findes, og `pvrsrvkm`
   står i `/proc/modules` (README'ens "GPU er indbygget" holder altså). Men den er
   kun døren til 3D-motoren; den udfører intet uden de to næste lag.
2. **Userspace-DDK** — de lukkede PowerVR-blobs der implementerer GLES
   (shader-kompiler, kommando-afsendelse). De findes **ikke** på dagens boks:
   V160309-imaget har ingen `/system`-partition (målt i den originale parameter og i
   `vendor_root`), og blobs'ene lå kun i dualOS-imagets Android-partition (V151129).
   Vendor-Lubuntu havde kun libhybris-broerne til dem i `/usr/local/lib`
   (libEGL/libGLESv2 + `libhybris`, bl.a. til Kodi og en Chromium 47). Aldrig taget
   med i Devuan — se §5.15 for vejene ind.
3. **Integration mod skærmen** — KMS/DRI (kernel + X-driver) eller libhybris+Gralloc.
   Ingen af delene: `/sys/class/drm` findes ikke, og X kører fbdev.

To ting forklarer hvorfor fbdev ikke bare var et valg (§5.10):

- **Displayet og GPU'en er to forskellige stykker hardware.** VOP'en
  (display-controlleren) sender pixels ud af HDMI; den drives her af `rk_fb` som dum
  framebuffer (`/proc/fb`: 5 fb'er). GPU'en renderer kun ind i buffere, når lag 2+3
  beder om det. Uden KMS er den eneste X-driver fbdev — vendor gjorde præcis det
  samme i deres egen Lubuntu.
- **Selv software-GL er spærret.** Firefox kræver EGL-X11 eller direkte (DRI-baseret)
  GLX. `Xorg.0.log`: *"AIGLX: Screen 0 is not DRI2 capable"* → kun IGLX (server-side
  swrast) initialiseres, og den vej bruger Firefox ikke. Derfor fejler WebGL, selvom
  llvmpipe står klar: GL mangler både en dør ind i X (DRI) og en dør ud til skærmen
  (KMS).

Diagnose på en kørende boks: `ls /sys/class/drm` (tomt), `ls /dev/pvrsrvkm` (findes),
`grep -E "AIGLX|IGLX" /var/log/Xorg.0.log`, `dpkg -l | grep mesa`, `apt-cache policy
chromium` (150.x findes til armhf).

Vej ud af webspil-dødvandet: **chromium** med sin egen software-GL (SwiftShader) —
behøver ingen system-GL. Forbehold: sandsynligvis `--no-sandbox` (samme
seccomp/syscall-403-klasse som §5.5 og HAANDBOG fælde 14), installeret i qemu-chroot
(§5.9), og alt renderes på CPU'en (8×A53) — simple spil har en chance, tunge ikke.
Hardware-vejen er libhybris-stakken (§8) — fortsat vurderet "lav værdi".

### 5.14 Chromium-forsøget der afslørede en svag strømforsyning (aug 2026)

Friskt flashet boks (første boot: `resize2fs` i kern.log), derefter
`apt-get install chromium` direkte på boksen — trods §5.9's anbefaling om qemu-chroot.
Installationen døde brat ~100 s inde i dpkg-udpakningen (17:28:57), boksen genstartede
sig selv 3 s senere. Fundene:

- **Ingen OOM, panic eller BUG i loggene** — og `/proc/sys/kernel/panic=0`, så en
  panik ville have *hængt* boksen, ikke genstartet den.
- **Logfilerne ender i NUL-blokke** (`dpkg.log`, `history.log`, `syslog`, `kern.log`):
  filstørrelsen var journalført, men data nåede aldrig ud af page-cachen — hård død
  midt i skrivning. Sidste ext4-commit 17:29:01 (dpkg.log og bootlog.txt med identisk
  mtime ned til nanosekundet).
- **RTC'en beholdt 2026-tiden** gennem nedbruddet → selv-reset uden strømtab
  (PMIC-klassen), ikke strømsvigt på stikket.
- dpkg efterladt i stykker: chromium `iHR` (manglende kontrolfiler) + 23 pakker
  ukonfigurerede.

Reproduceret med en stress-test (samme .deb-filer + 8 travle CPU'er + 600 MB dd):
død igen på samme "5V 2A"-adapter. **A/B mod en 2,4A-lader: samme test overlevet**
(load ~10 på 8 kerner i 4½ min, vdd_arm 1,3V ved 1,2 GHz). Dom: adapteren leverer
ikke 2A under belastning → brownout → PMIC-reset. Mærkaten på en stikforsyning er
ikke en garanti.

Løsning: boksen kører nu på 2,4A. dpkg repareret med force-remove + `apt-get -f
install` + autoremove. Værktøj til genskabelse: `devuan/stress_test.sh`
(overvågningslog i `/root/stress_mon.log` — `/tmp` ryddes ved boot). Hele beviskæden
i pædagogisk form: HAANDBOG.md fælde 17.

### 5.15 GPU-vejene: hvad kan PowerVR'en bruges til (aug 2026)

Status: `pvrsrvkm` er loadet og fuldt initialiseret (kernel-tråde kører: `pvr_timer`,
`pvr_sync_check_` m.fl.), men resten af stakken mangler. Tre veje ind:

**Vej 1 — CPU + `/dev/fb0` (virker i dag).** `mmap` på `/dev/fb0` giver fuld
pixelkontrol uden om X — det er hvad `fb_overscan.py` gør. Rækker til 2D, demoer,
kiosk-programmer; ikke til 3D/WebGL.

**Vej 2 — pvrsrvkm-broen direkte.** Bridge-protokollens ABI er dokumenteret i
vendor-kernens GPL-kilde (`geekboxzone/lollipop_kernel`: `pvrsrv_bridge.h`,
`pvrsrv_ioctl.h`). Et C-program kan åbne `/dev/pvrsrvkm`, lave
`PVRSRV_BRIDGE_DEVINFO`, allokere og mappe GPU-hukommelse. Men shader-kompileren
(USC) og kommando-strømmens format er lukkede — uden blobs kan der ikke køres
shaders. Resten er reverse-engineering i åre-klassen.

**Vej 3 — libhybris + vendor-brikkerne (eneste vej til ægte GLES).** Målt opgørelse
(aug 2026):

- **På boksen i dag: intet.** Der er ingen `/system`-partition (V160309-parameteren:
  uboot/trust/resource/boot/backup/linuxroot) og ingen blobs nogen steder — `UF`-flashen
  af det rene Lubuntu-image skrev partitionstabellen om, så den gamle Android-partition
  (med blobs'ene) er overskrevet.
- **I repoet (`vendor_root`): hele vendors integrationsværktøj** — libhybris med fire
  EGL-platforme (`eglplatform_fbdev/hwcomposer/null/surfaceflinger.so`), libEGL- og
  libGLESv2-broer, `test_hwcomposer`, `kodi.bin` med custom cmake, og en Chromium 47
  med GLES2-ozone-lag (`/usr/lib/chromium-browser/libs/libgles2_c_lib.so`). Alt — bortset
  fra selve blobs'ene.
- **Blobs'ene:** findes i TO kilder: (a) **vendors egen `system.img`-FIL** i
  `vendor_root/usr/local/share/libhybris/system.img` (207 MB, 10. jan 2016 — samme
  æra som kernen, den BEDSTE match) og (b) dualOS-imaget V151129's Android-`/system`
  (nov 2015 — ældre DDK, men virkede også). Det er (a), der bruges i opskriften
  nedenfor.

### Trin 0-1: UDFØRT og VERIFICERET (aug 2026)

Resultatet målt på boks 1: `GL_VERSION=OpenGL ES 3.1 build 1.4@3632227`,
`GL_RENDERER=PowerVR Rogue G6110`, 500 frames renderet gennem
program→hybris→Android-EGL→blobs→pvrsrvkm→G6110→gralloc/ION→hwcomposer→HDMI.
Opskriften (scriptet: `devuan/gpu/`):

0. **Læg stakken til rette** (alle brikker fra repoet): vendors `system.img` loop-montes
   ved `/system` (myinit-patchen nedenfor gør det automatisk), `ln -s /system/vendor
   /vendor`, hybris-broerne fra `vendor_root/usr/local/lib` i `/opt/hybris` (inkl.
   `libhybris/`-eglplatformerne og `test_hwcomposer`), platformerne OGSÅ i
   `/usr/local/lib/libhybris/` (hårdkodet sti i broen), og `/dev/graphics/fb*`-symlinks
   (devtmpfs nulstiller dem ved hver boot!).
1. **Start mini-Android'en:** `/system/bin/logd` + `/system/bin/servicemanager
   --standalone` + **`/system/vendor/bin/pvrsrvctl --start`** — den sidste ER
   init-processen; uden den svarer kernen "Driver initialisation not completed yet"
   for evigt (målt: bridge-ABI'en matcher, men ingen init-forbindelse).
2. **Kør GL** med `LD_PRELOAD=system_shim.so LD_LIBRARY_PATH=/opt/hybris
   EGL_PLATFORM=hwcomposer` — se `devuan/gpu/test_triangle.cpp` (vores eget program;
   vendors platform SEGV'er, hvis man giver `eglCreateWindowSurface` et nul-vindue,
   så programmet laver vinduet selv via `HWCNativeWindowCreate` + hwc-present-callback).

Fælderne fundet undervejs (alle målt og løst):

- **`cma=128M` på cmdlinen er afgørende.** CMA-heapen (ion-heap 4) kan som standard
  kun holde ÉN 1920×1080-buffer; den næste allokering fejler med **EPERM** (ikke
  ENOMEM!), gralloc returnerer -12, og vinduet dør med "allocated only 0 buffers".
  Løst ved at tilføje `cma=128M` til `parameter_emmc.txt` (skrevet med §9's
  dd-metode på den kørende boks — ingen reflash nødvendig).
- **glibc 2.41's `system()` fejler med EFAULT i hybris-processen** (environ-blokken
  ødelægges når bionic-bibliotekerne loader — execve kan ikke læse den). Vendors kode
  kalder `system("chvt ...")` og `system("find /sys/class/display ...")` under init.
  Løsning: `devuan/gpu/system_shim.c` — en LD_PRELOAD-shim der overtager `system()`
  og fork/exec'er med et rent env.
- **Hele bionic-`/system/lib` skal med** (ikke kun blobs'ene): libstlport,
  libbacktrace, libutils-kaskaden — hybris' indlejrede Android-linker resolver dem
  fra `/system/lib`.
- **Vinduets usage 0x1800 (HW_COMPOSER|HW_FB) patchet til 0x1000** i
  `libhybris-hwcomposerwindow.so` (to mov/orr-instruktioner; GAS-encodings i
  dokumentationen). NB: ikke isoleret om dette var nødvendigt efter cma-fixet.
- **`/dev/graphics` forsvinder ved boot** — symlinkene skal oprettes igen hver gang
  (`devuan/gpu/gpu_up.sh` gør det).
- **Efter hwc-sessionen kommer HDMI ikke tilbage uden strøm-cyklus.** Målt (aug 2026):
  efter testen + X-genstart loggede kernen fint `sucess output HDMI` ved hver
  re-enable (display-dansen), men TV'et modtog intet signal ("Kontrollér
  enhedsstrøm"), og ~4 s senere faldt linket (`cec_set_pa 0` + `hdmi remove from
  lcdc0`). Transmitteren leverer altså ikke TMDS-signal efter hwc-overtagelsen —
  software-dansen kan ikke vække den. Kur: strøm-cyklus (boks OG evt. TV — jf.
  boks 2's HDMI-erfaring i §10).

Trin 2 (nyttiggørelse) — stadig åbent: vendor-kodi-binæren fra 2016 til lokal video
(muligvis kørbar direkte nu), eller portér en moderne browser — den lange vej:
firefox/chromium taler kun EGL-X11/DRI, som kræver KMS. Vendors egen Chromium 47
beviser at en browser *kan* køre på stakken — men den er 10 år gammel, kan ikke
moderne web, og rammer 3.10's seccomp/syscall-problemer (fælde 14).

### 5.15a GLES-daemonen og exit(42): den tomme `glReadPixels`-plads (aug 2026)

Da vi byggede GLES-daemonen (M1), virkede `ping`/`fb` — men den første `render`
dræbte processen tavst. Fejlsøgningen (alle trin målt, ikke gættet):

1. **Symptom:** klienten fik BrokenPipe; daemon-loggen sluttede lige efter
   `scenes`-svaret.
2. **Misvisende først:** `dmesg` var tom for nedbrud (print-fatal-signals er
   slået fra på boksen), og strace viste at processen IKKE fik et signal — den
   afsluttede selv pænt med `exit_group(42)`.
3. **Display-dansen:** strace + log viste `[system-shim]`-kaldene
   (`chvt 7`, sluk/tænd `/sys/class/display/*/enable`, `chvt 11`, `chvt 7`) lige
   før døden — den "dans" som libEGL kører, når den rydder op.
4. **Disassembly af `libEGL.so.1.0.0`:** `refresh_display()` ER dansen
   (5 × `system()` + usleep), og `catch_exit_signals()` er en signal-fælde:
   sigaction på 4/6/7/11/15/20 + `sigsetjmp`; kommer man tilbage fra
   `siglongjmp`, køres `cleanup()` (= dansen) og `exit(42)`.
5. **Signalet:** genlæsning af strace-loggen (grep "SIG") afslørede
   `--- SIGSEGV {si_code=SEGV_MAPERR, si_addr=NULL} ---` på hovedtråden, lige
   efter malloc af readback-bufferen (2.073.600 bytes = 960×540×4).
6. **gdb-backtrace:** `glReadPixels_wrapper (glesv2.c:775)` kaldte adresse 0x0.
7. **Rodårsag (disassembly af `libGLESv2.so.2.0.0`):** wrapperen kalder den ægte
   funktion gennem en global slot i BSS (`_glReadPixels`, offset 0x101dc) — og
   den var NULL. Init'ens `android_dlsym("glReadPixels")` havde ikke løst
   symbolet, selvom den ægte funktion ER eksporteret af
   `/system/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so` (readelf-verificeret).
   Hvorfor `android_dlsym` fejlede, er stadig uforklaret (åbent punkt).
8. **Afviste hypoteser (målt):** FBO-readback vs default-framebuffer-readback —
   begge crash (problemet er readback generelt, ikke FBO);
   `GRALLOC_USAGE_SW_READ_OFTEN` på vinduesbufferen — ingen effekt;
   `EGL_PLATFORM=null` — crasher endnu tidligere ved `eglCreateWindowSurface`
   (NULL-deref i `android_createDisplaySurface`-vejen; B7-proben er dermed
   besvaret: null-platformen er ikke en genvej på denne boks).

**Løsningen (`patch_readpixels()` i `gles_daemon.c`):** efter EGL-init hentes den
ægte funktion med `hybris_dlopen("libGLESv2.so")` + `hybris_dlsym("glReadPixels")`
(altså gennem hybris' Android-tolk — samme vej wrapperen selv skulle have brugt),
og pointeren skrives ind i den tomme slot (`base + 0x101dc`). Dermed virker
`glReadPixels` — både fra FBO og fra default-framebufferen.

**Verifikation:** `readback_probe.cpp` — begge readback-veje OK, 1.036.800 pixels
ændret (trekanten dækker præcis halvdelen af skærmen); `gles_daemon` — alle
kommandoer (`ping`/`fb`/`scenes`/`render`/`clear`/`quit`) svarer ok, og et
mmap-dump af fb0 viser scenen i rect'en.

**Betydning fremover:** enhver readback (også browser-WebGL-eksperimenter) ville
have dødd på samme NULL-slot; patchen åbner vejen. Opslagsværket: HAANDBOG
fælde 18.

### Ny boks i samme tilstand

**Firefox skal IKKE fjernes** — GPU-stakken bor uden for imaget. Regnestykket:
imagets rootfs er låst til ~1,4 GB og er 84 % fuld *med* firefox (~225 MB fri), mens
stakken fylder ~205 MB (system.img 198 MB + broer/headere ~7 MB). Stakken røres
derfor ikke i 09 — den lægges på efter flash:

```bash
sudo devuan/testflash.sh                      # image med cma=128M (parameter_emmc.txt)
devuan/gpu/gpu_setup.sh <ny-boks-ip>          # system.img + broer + færdige binærer
# → strøm-cykl boksen
```

`gpu_setup.sh` kopierer vendors system.img og hybris-broerne fra repoet, patcher
usage-konstanten (0x1800→0x1000 via `patch_hwc_usage.py`), og henter de
færdigkompilerede binærer fra boks 1 — så den nye boks ikke behøver gcc/gdb/strace.
myinit (i imaget siden aug 2026) monterer `/system` automatisk, når system.img
findes, og er en no-op på bokse uden stakken.

**Værdi-vurdering (opdateret):** G6110 er 2016-mobil-klasse (GLES 3.1, 1080p
H.264-decode). GPU-adgangen er nu LØST og verificeret (trin 0-1, en aften med
målinger) — det, der stadig koster uger-måneder, er alene browser-integrationen
(trin 2).

### 5.15b `eglplatform_x11`-prototypen: GLES ind i et X-vindue (aug 2026)

**Formål (BROWSER-VEJE §2.A):** en rigtig libhybris-EGL-platform, så GPU-billeder
kan vises i et X-vindue, mens X kører — forudsætningen for Kodi og en browser.
M2b-vindue-demoen (socket + Python + XPutImage) beviste mekanikken, men Kodi/browser
kalder EGL direkte og kræver en platform, der selv præsenterer.

**Arkitektur (kode: `devuan/gpu/eglplatform_x11/`):**
- Platformen er et `eglplatform_*.so` i `/usr/local/lib/libhybris/`, der eksporterer
  `ws_module_info` (hele `ws_module`-vtabellen fra A3: init_module, GetDisplay,
  Terminate, CreateWindow, DestroyWindow, eglGetProcAddress, passthroughImageKHR,
  eglQueryString, prepareSwap, finishSwap, setSwapInterval).
- `CreateWindow` modtager XID'en og pakker den ind i en `X11NativeWindow`
  (BaseNativeWindow fra `nativewindowbase.h`), som allokerer gralloc-buffere
  (format RGBA_8888, usage `GRALLOC_USAGE_HW_FB|SW_READ_OFTEN` — målt: HW_COMPOSER
  giver ENOMEM i PVR-grallocen).
- `queueBuffer` → `gralloc->lock` (CPU-læsning) → RGB565-pakning (X' 16-bit
  visual) → XPutImage.
- Bygget på boksen (`build_box.sh`; armhf-X11-headere mangler på laptoppen).
  Testklient: `test_client_x11.cpp` (X-vindue + `EGL_PLATFORM=x11` + cos-scene).

**Verificeret (24. aug 2026):** 640x360-vindue viser cos-mønsteret på fb0 —
100 unikke farver (0xFFFF, 0x0000, varme cos-farver 0xFF36 osv.), 6.208 px
forskellige mellem to fbdump midt i kørslen (fase-sweep kører), ~9 fps @ 640x360,
`GL_VERSION=OpenGL ES 3.1 build 1.4@3632227`, `GL_RENDERER=PowerVR Rogue G6110`,
aktiv VT og HDMI-enable urørt bagefter. libEGL dlopen'er `eglplatform_x11.so` og
kalder `ws_module_info` — A3-kontrakten holder.

**To målte fælder (begge løst i platformen):**
1. **Hybris' EGL-init skifter aktiv VT væk fra X' VT (målt: →vt10).** fbdev-X'
   shadow-framebuffer kopieres kun til fb0, når X' VT er aktiv — ellers viser
   xwininfo IsViewable, men fb0 er urørt (al tegning "forsvinder"). Fix:
   `ensure_x_vt()` — find Xorgs VT via `/proc/<pid>/cmdline` (NB: NUL-adskilte
   argumenter, se fælde 22) og ioctl `VT_ACTIVATE`+`VT_WAITACTIVE` på `/dev/tty0`,
   kaldt ved første present. NB: `popen`/`pgrep` fejler i hybris-processer
   (ødelagt environ → execve-EFAULT, fælde 21) — derfor direkte `/proc`-scanning.
2. **Tegning fra en anden X-forbindelse end vinduets egen når ikke fb0 på denne
   server** (requests accepteres uden fejl, intet renderer). Fix: klienten sender
   sit `Display*` som EGL-native-display (`eglGetDisplay((EGLNativeDisplayType)dpy)`);
   platformens `GetDisplay` gemmer det, og `present()` tegner via klientens
   forbindelse.

**Fejlsøgnings-sporet (kort):** solid-farve-test → 0 px i fb0 trods korrekte
request-streams (strace `writev` byte-identiske); XGetImage læste sort;
xdraw-probe viste at settle + samme-forbindelse virkede; `chvt 8`-testen isolerede
VT-fælden. LD_PRELOAD var et vildspor (tilfældig korrelation). Fælder 19-22 i
HAANDBOG.md.

**Næste skridt:** afprøv platformen med en rigtig app (Kodi linker direkte mod
hybris libEGL/libGLESv2 og kan køres med `EGL_PLATFORM=x11`), derefter stock
Firefox + `MOZ_X11_EGL=1` (BROWSER-VEJE eksperiment 2).

### 5.15c Firefox-forsøget: Android-loaderens `eglGetDisplay` vinder (aug 2026)

**Forsøg** (BROWSER-VEJE eksperiment 2, 24. aug 2026): firefox-esr 140.12 med
`MOZ_X11_EGL=1`, `LD_LIBRARY_PATH=/opt/hybris`, `EGL_PLATFORM=x11`,
`LD_PRELOAD` (system_shim + egl_platform_shim), `DISPLAY=:0`, temp-profil.

**Målt:**
- Firefox' GL-probe (`glxtest`) dlopen'er vores `libEGL.so.1` og loader via
  bionic hele Android-EGL-kæden: `/system/lib/libEGL.so` (Android-loaderen) +
  `/vendor/lib/egl/libEGL_POWERVR_ROGUE.so` (bekræftet med strace + logd
  "loaded ...").
- Derefter fejler `eglGetDisplay` med **EGL_BAD_DISPLAY** (logd:
  "eglGetDisplay:218 error 300c") → glxtest melder "libEGL no display" og
  Firefox falder tilbage til Mesa-software (llvmpipe, GLX).
- De samme kald virker i kloner: `dlopen_egl_test.cpp` (dlopen → dlsym →
  `eglGetDisplay(EGL_DEFAULT_DISPLAY)` → 0x1, eglInitialize 1.4) og
  `egl_display_probe.cpp` (også med `Display*` og `eglGetPlatformDisplayEXT`
  via shim); `x11ws: init_module` kører.

**Arbejde udført:**
- Platformens `eglQueryString`-hook annoncerer nu client-extensionerne
  `EGL_EXT_platform_base` + `EGL_EXT_platform_x11` + `EGL_KHR_platform_x11`
  (målt nødvendige for Firefox' probe).
- `egl_platform_shim.c` (LD_PRELOAD) eksporterer `eglGetPlatformDisplayEXT`,
  `eglGetPlatformDisplay` og `eglGetDisplay` (viderestiller til wrapperen).
- Diagnose-værktøjer i `devuan/gpu/eglplatform_x11/`: `dlsym_trace.c`
  (log dlopen/dlsym med egl-navne), `dlopen_egl_test.cpp`,
  `egl_display_probe.cpp`.

**LØST — hvorfor glxtest ramte Android-loaderens `eglGetDisplay`:**
Rodårsagen var hverken display-argumentet eller bionic-namespace — det var
**symbolopslagsmetoden**. glxtest henter kerne-EGL-funktioner gennem
`eglGetProcAddress("eglGetDisplay")` (se `get_egl_status` i
`toolkit/xre/glxtest/glxtest.cpp`), ikke via dlsym. Wrapperens
`eglGetProcAddress`-kæde er: special-cases → dlsym(platform-handle) →
`ws_eglGetProcAddress` (platformens egen) → Android-loaderens interne
funktioner. Vores platforms `ws_eglGetProcAddress` returnerede NULL for
kerne-EGL-navne (delegere til `eglplatformcommon_eglGetProcAddress`), så
Android-intern `eglGetDisplay` (+0x50c0, kun r0==0 accepteres, linje 218/300C)
vandt. Probe-bevis: `eglGetProcAddress("eglGetDisplay")` gav Android-intern
(afviste 0x1234), mens `dlsym(libegl, "eglGetDisplay")` gav wrapperens
(håndterede 0x1234). De tidligere klon-prober testede dlsym-vejen og var
dermed et vildspor.

**Fix (24. aug 2026):** platformens `ws_eglGetProcAddress`
(`devuan/gpu/eglplatform_x11/eglplatform_x11.cpp`) videresender kerne-EGL-
navne til wrapperens egne eksporter (dlopen + dlsym af
`/opt/hybris/libEGL.so.1`, undtagen `eglGetProcAddress` selv — rekursion) og
tilbyder stubs for `eglQueryDeviceStringEXT`/`eglQueryDisplayAttribEXT`.
Derudover patchet glxtest-binæren: `cmp r1, #24` → `#16` (offset 0x2777;
backup `/root/glxtest.orig`) — boksens X er 16-bit, og uden patchen smed
proben det ellers vellykkede EGL-resultat væk (Bug 1667621).

**Resultat:** `glxtest` melder nu `TEST_TYPE=EGL`, `VENDOR=Imagination
Technologies`, `RENDERER=PowerVR Rogue G6110`, `OpenGL ES 3.1
build 1.4@3632227` — ingen Mesa/GLX-fallback.

**Ny blokering (fuld Firefox, kørt 24. aug 2026):** WebRender-hardware-
kontekst fejler → "Fallback WR to SW-WR". To mønstre målt med gdb:
0x300c (Android-intern create-vej — LØST med bindAPI/chooseConfig-patches,
se nedenfor) og 0x3000 (kontekst-`Init` fejler efter vellykket
create+MakeCurrent).

**LØST — 0x3000's rodårsag var Mesa-libGL-shadowing (25. aug 2026):**
Firefox' `GLContext::InitImpl` loader alle kerne-GL-symboler via
`SymbolLoader::GetProcAddress`, som slår op i `libGL.so.1` (dlsym) FØRST og
kun falder tilbage på `eglGetProcAddress`, hvis dlsym fejler
(`GLLibraryLoader.cpp`). Boksens `/lib/arm-linux-gnueabihf/libGL.so.1` er
Mesas vendor-dispatch — den loades desuden allerede i processen, fordi
hybris-wrapperens init kalder `dlopen("libGL.so")`. Resultat: alle
`gl*`-symboler kom fra Mesa (0 `eglGetProcAddress`-kald efter MakeCurrent,
målt med `egl_trace_lib.c`), og `glGetError()`/`glGetString()` blev kaldt på
Mesa uden Mesa-kontekst → Init fejlede stille → 0x3000.

**Fix:** tomme stub-`libGL.so`/`libGL.so.1` (ingen gl*-eksporter, korrekt
SONAME) i `/root/glstub/` først i `LD_LIBRARY_PATH` — dlsym fejler på hvert
navn, `eglGetProcAddress`-vejen (→ PowerVR GLES) vinder, præcis som i
glxtest. Verificeret: `GL version detected: 310`, `OpenGL vendor:
Imagination Technologies`, `OpenGL renderer: PowerVR Rogue G6110`, ingen
SW-fallback.

**Andet fund (25. aug):** kompositorvinduet blev skabt 1x1 og Android-EGL'en
spurgte kun størrelsen én gang → overfladen frøs på 1x1 og compositoren nåede
aldrig første present. Fix i `eglplatform_x11.cpp` (`X11NativeWindow`):
`width()/height()/defaultWidth()/defaultHeight()` og `queueBuffer()` henter
den levende X-størrelse (`refresh_size()`), `dequeueBuffer()` reallokerer ved
ændring, og `x11ws_CreateWindow()` venter KORT på reel størrelse (200 ms =
5×40 ms; de oprindelige 50×40 ms = 2 s gav main-processen tid nok til at nå
sit GPU-reply-timeout og dræbe GPU-processen med "IPC reply timeout").

**WebGL 2.0 er dermed STABILT målt virkende** (25. aug, NORMAL kørsel — se
`devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-25.md`): `WEBGL_RESULT OK
PowerVR Rogue G6200, or similar WebGL 2.0` efter 3 tegnede frames, med
`x11ws: present #2 (1280x948 ...)`, vinduestitel `OK PowerVR Rogue G6200,
or similar WebGL 2.0 — Mozilla Firefox` og 0 X-fejl. De sidste to fund samme
dag: (a) den formodede "channel-error-race" var dels vores egen
`pkill -9 -x firefox-esr` (som kun rammer main — børnene hedder "GPU
Process"/"file:// Content" osv. via prctl og lukker kanalen med exit(0) ved
main's død), dels `MOZ_GL_SPEW=1`, hvis KHR_debug-callback lammer
compositoren (ingen present, siden loader ikke) — uden variablen kører alt
normalt. (b) `XPutImage` fejlede BadMatch (request 72), fordi
kompositorvinduet er TrueColor depth 32, mens vi tegnede et 16-bit XImage
med root'ens default-GC → sort vindue; fixet: vinduets egen visual/dybde +
dedikeret GC + eksplicit 32-bit ARGB-konvertering. De øvrige 24. aug-patches:
Android-bindAPI-normalisering + chooseConfig-ES2-sti
(`patch_android_bindapi.sh`) og driverens minor2-bhi
(`patch_driver_minor.sh`) — alle stadig aktive på boksen.

Verifikation når konteksten virker: about:support + platformens præsent-log
(`x11ws: vindue pakket ind` / `present`) + fbdump.

**Desktop-genvejen er klik-verificeret (25. aug 2026):** `firefox-webgl`
kører fra selve LXDE-sessionen (samme Exec-linje som et klik på
"Firefox WebGL", startet som kristian med sessionens miljø — lxpanel/
pcmanfm kørte under testen): `WEBGL_RESULT OK`, present #1 (1x1) → #2
(1280x948) → #50, korrekt titel via `_NET_WM_NAME`, 0 X-fejl, 0 "alle
buffere er busy", gradient synlig i fb0-dump. Genvejen ligger med exec-bit i
`/home/kristian/Desktop/` og `~/.local/share/applications/`.

**Klikket afslørede to ting, der havde været der hele tiden (25. aug, aften):**
(a) WebRender-hardwarestien renderer HTML/CSS-indhold SORT i
EGL/gralloc-bufferen (sort chrome-område + hvide felter; divs/tekst bliver
sort, mens hvid baggrund og WebGL-canvas overlever — Firefox' eget
`--screenshot` viser siden korrekt). Fix: **Basic-kompositor** i profilen
(`gfx.webrender.enabled=false` + `layers.acceleration.disabled=true`) — hele
UI'et tegnes da korrekt via X, og WebGL virker stadig (GLES via hybris/PVR;
`WEBGL_RESULT OK` + canvas på skærmen). (b) Firefox sætter selv
`_MOTIF_WM_HINTS` decorations=0 → ingen titelbjælke/knapper (openbox-regel
overskriver det ikke; drawInTitlebar=false hjælper ikke). Fix:
`firefox-webgl`-launcher'en sætter `_MOTIF_WM_HINTS = 0x2,0x1,...` når
Navigator-vinduet er fremme — verificeret: client Relative Y=23, knapper
synlige. Fælde: en testside med rød body-baggrund fik boksen til at slukke
HELT to gange (ingen panic-log i 3.10-kernen) — kør ikke den test.

### 5.15d GL-layers-forsøget: spillet animerer i vinduet — skærmen tier (25. aug 2026)

Situationen efter §5.15c: WebGL 2.0 og hele Firefox-UI'et virker med
Basic-kompositoren, men **Subway Surfers (poki.com) frøs efter første frame**
(målt: 0 pixel ændring over 36 s i spilvinduet; main ~118 % CPU fordelt på
mange tråde à ~5 % — ingen enkelt spinner; content-hovedtråden lavede ~3.800
`clock_gettime`-kald/sek (busy-wait); SoftwareVsyncThread ~1.800/sek; ingen
GPU-proces). Det lignede en livelock i vsync/present-stien, ikke langsom
readback.

**Eksperiment (en variabel):** `layers.acceleration.disabled` true → false
(behold `gfx.webrender.enabled=false`) — gamle GL-layers-kompositor via EGL.
Resultater, alle målt:

- **Testside** (`webgl_test_dump.html`): `WEBGL_RESULT OK PowerVR Rogue
  G6200, or similar WebGL 2.0`, GPU-proces kører, present-kæden kører
  (#50 → #100 → ...), CSS renderer korrekt (body `#222` = præcis #222, IKKE
  sort som WebRender-fejlen), og **efter genstart målt at animationen når
  BÅDE X-root (518.481 px/2 s) og fb0 (329.280 px/2 s)** — GL-layers virker
  end-to-end for den simple side.
- **Spillet:** første frame når skærmen (~126.416 px ændret i vindueområdet
  på root), hvorefter præsentationen til root/fb0 stopper — MEN vinduets
  egne pixels fortsætter med at animere (70–168k px/2–4 s), present-kæden
  sænker farten til ~5 fps (#250+ vokser langsomt; loggen skriver hver 50.).
- **To svigtmønstre målt:** (a) GPU-proces `DeviceReset
  DeviceResetReason::UNKNOWN DeviceResetDetectPlace::WR_POST_UPDATE` ~50
  frames inde → ny GPU-proces gen-wrapper samme vindue, presents fortsætter
  ind i vinduet (op til #900), men skærmen forbliver sort; (b) uden reset:
  present-kæden sænker til ~5 fps og skærmen stopper efter første frame(s).
  Begge ender med `CompositorBridgeChild ... AbnormalShutdown`, `accel
  canvas lost`, channel error — og **to kørsler tog hele boksen ned**
  (strøm-cykling nødvendig; ingen panic-log i 3.10-kernen, som ved
  body-rød-testen i §5.15c).
- **Prober (nye værktøjer i `devuan/gpu/eglplatform_x11/`):** `x32probe1–4.c`
  beviser at X-serveren KAN kompositerer 32-bit vinduer/children til root OG
  fb0: openbox-framet, Firefox' visual 0x1ec, separat X-forbindelse som
  tegner, XSync efter hver frame, gentagne XPutImage (grøn/blå skifte synligt
  frame-for-frame på root og fb0). Vindues-attributter (depth 32, visual
  0x1ec, colormap 0x100002a ikke installeret, backing NotUseful, gravity)
  matcher Firefox' vinduer. **Fejlen er altså Firefox/GPU-genstart-specifik,
  IKKE en X-server-begrænsning** (uafklaret).
- **Lokal stress-side (25. aug nat, efter §5.15d's plan trin 1):**
  `webgl_stress.html` (fuld canvas, kontinuerlig rAF-animation, FPS via
  `dump()`, justerbar opløsning `?scale=&tiles=&tex=`) kørte i GL-layers med
  måle-loop (`capture_stress.sh`: root/fb0/vindue-diff hver 4 s). De første
  4+ min opdaterede skærmen kontinuerligt (root ~1.040.000 px/4 s, fb0
  ~437.000 px/4 s; present #2→#150; rAF-FPS ~1,5) — derefter kilede det:
  `xdump` (XGetImage root) og `dd if=/dev/fb0` gik i D-state (uafbrydelig),
  GPU-processen spindede ~100 % CPU, load steg til 13+; sysrq-b-genstart
- **Kontrol-kørsel uden måle-læsninger (run C, 22:05–22:16):** stress-siden
  kørte 8+ min; GPU-processen fik `DeviceResetReason::UNKNOWN
  WR_POST_UPDATE` ~1 min inde, og derefter:
  - Bruger-observation: resten af skærmen har fine farver — KUN Firefox-
    vinduet er sort (openbox-titellinjen med teksten er læsbar).
  - Vindue-dump (XGetImage af Navigator, depth 32) er ~100 % sort
    (middel-lysstyrke 0-2, 0 % lyse pixels); de ~78k px/4 s ændringer er
    støj. rAF/FPS kører videre, presents fortsætter (#800+), men indholdet
    er sort. Ingen `CONTEXT_LOST`, ingen GL-fejl i loggen.
  - gdb på GPU-processen efter reset: Renderer-tråd i `present()`s
    pixel-konverteringsloop (normalt arbejde — presents KØRER).
  - xrefresh hjælper ikke på det sorte indhold.
  - **REVIDERET KONKLUSION: frysen er en RENDER-fejl efter GPU-proces-
    genstart — WebRender tegner sorte frames ind i EGL-overfladen. X-serveren
    og kompositeringen virker (desktop + openbox-ramme + øvrige vinduer
    vises fint).** Den tidligere "present-stien er synderen"-fortolkning
    (fra run 6, hvor måle-læsningerne selv kilede fb-driverens read() i
    D-state) er hermed skilt ad: fb-read-wedge er et separat kernel-problem
    i måle-værktøjerne.

**Konklusion (revideret):** GL-layers-stien løser spillets render-livelock,
men efter ~1 min kollapser GPU-processen (`WR_POST_UPDATE`), og den
genstartede GPU-proces renderer SORT indhold (rAF/presents kører videre).
Det er forklaringen på "vinduet animerer, skærmen tier": vinduets X-side
ændres kun af støj/UI, og skærmen viser et sort frosset vindue. X-serverens
kompositering er sund. Basic-kompositoren (status quo i §5.15c) er fortsat
den stabile opsætning for UI + WebGL-test.

**Næste skridt (aftalt):**
1. ~~Lokal WebGL-stress-side~~ — **FÆRDIG:** fejlen reproduceres; rodårsagen
   er GPU-proces-reset (WR_POST_UPDATE) → sorte frames efter genstart.
2. **Find det GL-fejl der udløser reset'et** — kør med Firefox' egen
   gfx-logging (`MOZ_LOG="gfx:5"` eller `gfx.logging.level`-pref, IKKE
   MOZ_GL_SPEW) og fang fejlkoden omkring `WR_POST_UPDATE`; test også om
   reset'et er frame-antal-afhængigt (let vs. tung side, `scale`/`tex`).
   NB: `MOZ_LOG=gfx:5` gav intet output i run D — logningen er blind i denne
   build; alternativet er nedenstående standalone-test **eller
   `RUST_LOG=webrender=debug`** (se Bugzilla 1989579 nedenfor).
3. **Standalone GLES-loop gennem samme eglplatform_x11-sti**
   (`test_client_x11`, 3+ min): fejler vendor-stakken selv (eglSwapBuffers-
   fejl/sorte frames efter N swaps) → roden er hybris/PVR/gralloc, ikke
   Firefox; kører den fint → fejlen er Firefox/WebRender-samspillet.
   **UDFØRT (22:27): 300 swaps uden fejl (2,2 fps) — vendor-stakken
   overlever; fejlen er Firefox/WebRender-specifik.** NB: test_client
   overflade blev fmt=4 (RGB_565) og shim'ens konvertering er hårdkodet
   RGBA8888 → 2×2-tiling/artefakter i testklienten (fix: respekter b->format).
   **Også UDFØRT (23:41): `gl_reset_probe` — 300 frames 1280×720 med
   glGetError/eglGetError pr. frame → 0 afvigelser** (bevis:
   `devuan/gpu/beviser/gl_reset_probe-2026-08-25.log`). Vendor-GL er ren;
   reset'et er WebRender-intern.
4. **uBlock Origin** i profilen som kontrol (mindsker reklame-SDK-load og
   dermed måske GPU-reset-risiko; reklamer er IKKE årsag til frysen, men kan
   bidrage til reset/nedbrud).
5. **Workaround-jagt:** automatisk genstart af Firefox når canvas'et bliver
   sort (frisk proces renderer korrekt) — eller undgå reset via prefs.
   Status 25. aug nat: reset'et er flaky (~50 % af kørslerne; 2 af 4 på
   denne boot), uafhængigt af canvas-størrelse; frisk Firefox-start efter
   reset renderer korrekt, så auto-genstart er en brugbar nødløsning.
6. **Buffer-race-hypotesen (NY, primær):** Firefox opretter kompositorvinduet
   1×1 og resizer det bagefter; `destroyBuffers()` i eglplatform_x11.cpp
   sletter buffere uden at tjekke `busy` → use-after-free → sporadisk
   GL-fejl → WR_POST_UPDATE-reset. Standalone-klienten laver ikke 1×1-dansen
   og reseter aldrig — stærk indirekte bevis. **Fix:** slet aldrig en busy
   buffer (retire + frigør ved queue/cancel), genbyg, mål reset-raten over
   4-6 kørsler. Derefter: swap/queue-fejllogning, kompositor-verifikation
   (WebRender vs. gammel GL-layers), resize-reproduktionstest, og til sidst
   kadence-problemet (~1,4 Hz).
7. **Andre WebGL-sider som andet datapunkt** (aftalt i sidste session,
   manglede i dokumentationen): **Shadertoy** (shadertoy.com, pure fragment-
   shaders, ingen reklamer, anden kodevej end Unity), **WebGL-aquarium /
   three.js-eksempler** (mange draw-calls, kontinuerlig animation, ingen
   annoncer), og til sidst **Basemark WebGL** (rigtigt benchmark, men
   nedbrudsrisiko for boksen). Det adskiller tung fragment-belastning fra
   Unity-engine-problemer.

### 5.15e GPU-MMU-fault bag WR_POST_UPDATE-reset + precache-determinisme (26. aug 2026)

**GENNEMBRUD:** reset'et er en RIGTIG GPU-fejl, ikke en userspace-
falsk-positiv. dmesg viser PVR-kernens fault-recovery (tidligere "dmesg tavs"
var forkert — dmesg-ringen fyldes med syscall-403-flood på få minutter, så
beviset skal fanges straks):

```text
PVR_K: RGX BVNC: 5.9.1.46
PVR_K:   Recovery 1: PID = 2116, frame = 0, HWRTData = 0x400F1500,
         EventStatus = 0x00000010, CRTimer = 0x000000036CF1, Innocent Lockup
PVR_K:     BIF0 - FAULT:
PVR_K:       * MMU status (0x0000000000007001): PC = 7, Page Size = 0, ...
PVR_K:       * Request (0x00040E0102FF9540): MCU (128bit word within the Lower
                 256bits, TPUA_USC, Banks 0-3), Reading from 0x0102FF9540.
PVR_K: FW logged fault using PC Address: 0x000000005FBB4000
```

TPUA_USC (tekstur-PU'en) læser fra en **umappet GPU-adresse** — klassisk
for-tidligt frigivet/ummappet hukommelse (use-after-free-mønster). Driveren
recoverer ("Innocent Lockup", ødelægger konteksten) → Firefox detekterer
`WR_POST_UPDATE` → GPU-proces-genstart → WebRender tegner sorte frames.
gdb-fangsten fangede desuden SIGSEGV i Renderer-tråden (0x0) i den genstartede
GPU-proces.

**Precache-determinisme (målt 3×):** med `gfx.webrender.precache-shaders=true`
fejler `cs_border_segment` ved `wr_shaders_resume_warmup` ("Compile failed."),
og reset kommer deterministisk ved present #50–#150 (~1–3 min). Uden precache
kompilerer samme shader Success og reset er flaky (#2–#950 / aldrig).
Shaderen er ikke "i stykker" (kompilerer ved normal opstart) — warmup-fejlen
er trigger/indikator for det underliggende GPU-fault-mønster.

**Fangstværktøjer (repoet, `devuan/gpu/eglplatform_x11/`):**
`gl_capture_shim.c` (LD_PRELOAD; virker standalone, men Firefox' GPU-proces
kalder ikke vendor-lib'ens eksporterede GL-funktioner direkte → intercept
virker ikke i Firefox), `gdb_wr_reset.cmd` + `capture_gdb_gpu.sh` (gdb med
adresse-breakpoints på vendor-libs: glCompileShader GLES2+0x3895c,
eglMakeCurrent EGL+0x11d4, glGetError GLES2+0x2308c). Beviser:
`devuan/gpu/beviser/ff_precache5-2026-08-26.log` + `gdb_wr_reset-precache5.log`.

**Næste skridt:** find ud af HVAD der er unmappet (shim-buffer-lifecycle
1×1→resize-dansen vs. WebRender-teksturcache; test 3–4 buffere + fence før
frigørelse), og fang dmesg straks efter reset (automatisk `dmesg -c` i
start_game.sh). Fuld detalje: `devuan/gpu/GPU-FAULT-GENNEMBRUD-SESSION-NOTAT-2026-08-26.md`.

**Buffer-fix installeret og verificeret (26. aug, 01:1x):** `destroyBuffers()`
retirer nu ALLE buffere (ikke kun busy) og frigør dem først efter 3 presents
(`retire_old()`, md5 `4ba7a90d`). Målt på stress-siden uden precache: present
#350+ med 0 resets (tidligere reset ved #2–#150 i ~50 % af kørslerne), **0
PVR-faults i dmesg** og **FPS ~2,7 mod tidligere ~1,5**. eglMakeCurrent-
overvågning (1469 kald) viste 0 fejl. Årsagen til forbedringen: ingen
gralloc-reallokering/re-mapping ved hver resize — GPU'en får tid til at blive
færdig med bufferen før frigørelse.

**Subway Surfers' frys = shader-kompileringsblokade (26. aug, 01:5x):** spillet
(Unity WebGL1) bruger `#extension GL_EXT_draw_buffers : require` + `#extension
GL_EXT_frag_depth : require` i sine fragment-shaders — og driver-kompileren
afviser begge ("Extension not supported"). Målt direkte (`shader_ext_test.c`):
`GL_EXT_draw_buffers` afvises på både ES2 og ES3, SELVOM den står i
GL_EXTENSIONS (driver-inkonsistens); `GL_EXT_frag_depth` er slet ikke i
GL_EXTENSIONS. Konsekvens: spillets render-shaders kompilerer ikke → ingen
frames præsenteres → det kendte frys (present #2, load stiger). Dette er den
EGENTLIGE spil-blokade; GPU-reset/buffer-fix var en separat mekanisme på
stress-siden. **WebGL1-tvang afkræftet (02:1x):** `webgl.enable-webgl2=false`
slår WebGL2 fra og driver-patch (fjern GL_EXT_draw_buffers fra GL_EXTENSIONS,
bind-mount, md5 acaad3bc) virker — men spillet fejler stadig, fordi Unitys
EGNE shaders kræver MRT via direktivet, og kompileren afviser det uanset
udvidelseslisten (målt isoleret med `trivial_test.c`: triviel shader OK, shader
med direktivet FEJL). **Konklusion: MRT-blokaden er en 2016-æra driver-
begrænsning, ikke en konfigurationsfejl.** Veje videre: nyere DDK (research-
spor), spil uden MRT-shaders, eller accept. Bevis:
`devuan/gpu/beviser/ff_game2-subway-2026-08-26.log` +
`devuan/gpu/beviser/ff_game5-webgl1-tvang.log`.

**DDK-sporet (26. aug 2026): DDK 1.5@3830101 fundet og PRØVEINSTALLERET med
NEGATIVT resultat.** Kilde: `leddaz-dump-stash/android_rk3368_box_dump`
(GitHub, Android 6.0.1 rk3368_box, bygget nov 2018) — 32-bit userspace + arm64
`pvrsrvkm.ko`. 1.5's `libglslcompiler.so` indeholder `GL_EXT_draw_buffers`
(1.4's gør ikke), men **1.5-userspace hænger boksens indbyggede KM hårdt ved
EGL-init (2× bekræftet wedge, anden gang på rent filsystem + 6.0-libc)**.
Vigtige målinger: (a) **pvrsrvkm er bygget ind i kernen** (tom /proc/modules,
kallsyms-symboler, "Module unloading is not supported", bootlog
`sys.gpvr.version=Rogue L 0.22` ved 2,7 s) → KM kan ikke byttes som .ko;
(b) 1.5-UM kræver Android 6.0's bionic-libc (`__register_atfork` — findes ikke
i 5.1-libc, kun `__cxa_atexit`); (c) dumpets `pvrsrvctl` er 64-bit (ubrugbar på
32-bit userspace); (d) 1.5's `libIMGegl.so` har minor-tjek på 0xa180 (ikke
0x9194 som 1.4). DDK 1.8 er kun fundet til kernel 4.4. Restore til 1.4
gennemført og verificeret. Fuld detalje: `devuan/gpu/DDK-PROEVEINSTALLATION-
SESSION-NOTAT-2026-08-26.md`; plan/backup: `devuan/gpu/DDK-HANDOVER-2026-08-26.md`.
**Løsning dokumenteret (26. aug ~04:2x):** kernel-rebuild med 1.5-KM fra
`geekboxzone/mmallow_kernel` (gren `geekbox`, `drivers/gpu/rogue`) indbygget i 3.10
(helst i samme hug opgraderet til 3.10.108, jf. DRIVER-PORTERING.md §6) + løsning af
de to blokader — se `devuan/gpu/DDK15-KERNEL-REBUILD-LØSNING-2026-08-26.md`.

**Bugzilla-signaturmatch (25. aug nat):** bug
[1989579](https://bugzilla.mozilla.org/show_bug.cgi?id=1989579) (dup af
1986254 → dup af 1667748) viser præcis vores sekvens:
`DeviceResetReason::UNKNOWN WR_POST_UPDATE` efter
`[ERROR webrender::device::gl] Failed to compile vertex shader:
ps_text_run_ALPHA_PASS_TEXTURE_2D` → `wr_renderer_render:
Shader(Compilation(...))` → "Handling webrender error 2". På desktop var
rodårsagen en dma-buf-fd uden CLOEXEC der arves af child-processer
(driver-korruption). Vores boks er PowerVR/hybris (anden stak), men
signaturen giver et konkret mål: fang den fejlende shader med
`RUST_LOG=webrender=debug` (MOZ_LOG-gfx-vejen er blind i denne build).

**Run G (26. aug nat, RUST_LOG=webrender=debug) afkræfter shader-hypotesen:**
fuld webrender-logning virker på ESR (modulnavn efter niveauet:
`[INFO  webrender::device::gl]`), ALLE shaders kompilerede med "Success"
(inkl. `ps_text_run_ALPHA_PASS_TEXTURE_2D` og
`composite_FAST_PATH_TEXTURE_2D` fra 1989579), og reset kom alligevel ved
present #950 / ~11 min — `DeviceResetReason::UNKNOWN WR_POST_UPDATE` UDEN
shader-/GL-fejl forud, og dmesg er tavs (ingen PVR/ION/fence-linjer).
Reset-tidspunktet spænder altså hele sessionen (tidligere målt: #2–#100).
Bevis: `devuan/gpu/beviser/ff_rust_runG-2026-08-25.log`. Ny arbejdshypotese:
driveren melder context-lost/ukendt status til WR_POST_UPDATE uden logget
GL-fejl — fang værdien med GL-shim/gdb. PowerVR-G6110 er desuden
WebRender-blokeret på Android (bug 1742987 pga. 1742986 + 1717863) og
Rogue-GPU'er har kendt glFenceSync-nedbrud (bug 1773128) — vores
desktop-Linux-boks rammes ikke af bloklisten.

Oversigt over nye værktøjer: `xdump.c` (XGetImage-dump),
`rootdiff.c` (16/32-bpp-diff på boksen), `capture_game_black.sh` +
`capture_stress.sh` (snapshot-loop root+fb0+vindue-attributter, tidslinje
med diffs), `stall_capture.sh` (gdb ved present-stall, rettet),
`start_game.sh` (instrumenteret spil-launcher), `x32probe1–4.c`
(X-kompositerings-prober), `webgl_stress.html` (lokal stress-side). Hele forløbet:
`devuan/gpu/GL-LAYERS-SESSION-NOTAT-2026-08-25.md`.

## 6. Slutarkitekturen

```
SD-kort (hele systemet)                 eMMC (urørt undtagen 1 parameter)
├─ partition 1: ext4 label=sdrootfs1    ├─ loader@0x40, uboot@0x2000, trust@0x4000
│  ├─ Devuan Excalibur armhf            ├─ resource@0x6000, boot@0xE000 (kernel+initramfs)
│  ├─ /root/myinit.sh  (PID1-shim)      └─ parameter@0: root=/dev/mmcblk1p1 init=/root/myinit.sh
│  ├─ /etc/asound.conf (vendor dmix)
│  ├─ /opt/alsa-da (daedalus libasound2)
│  └─ /swapfile (2 GB)
```

Boot-flow: BootROM → loader → U-Boot → parameter (SD + myinit) → vendor-kernel +
initramfs fra eMMC → mount SD-partition → **myinit.sh** (mounts, netværk kun ved link,
fb-normalisering, dropbear, bootlog) → **sysvinit** → rcS (eudev, netværk via
ifupdown, fsck) → rc2 (chrony, network-manager, **nodm**) → X fbdev → LXDE som `kristian`.

SSH: dropbear port 22, kun nøgle-login (`-s`).

## 7. Genskabelses-guide

Forudsætninger: denne repo klonet + downloadede artefakter (se README §Kilder).
Boksen skal have Lubuntu-boot-kæden på eMMC første gang (`UF` flash, se README).

```bash
# 1. Byg rootfs (sudo; bruger Devuans egen debootstrap via qemu)
sudo devuan/01_build_rootfs.sh
# 2. dropbear + myinit i rootfs
sudo devuan/06_install_dropbear.sh
# 3. Skriv SD-kortet (SLETTER kortet; ext4 uden 3.10-uvenlige features + swapfil)
sudo devuan/02_write_sd.sh /dev/sdX
# 4. Desktop + lyd: se devuan/07_desktop_audio.sh (pakkeliste + config ændret live)
#    07 er headless (debconf preseeded: dansk tastatur, nodm som display manager)
#    og idempotent — kan genkøres frit uden interaktion
sudo devuan/07_desktop_audio.sh
# 4b. (valgfri) NetworkManager til wifi — kortet i læseren, non-destruktiv:
sudo devuan/08_network_manager.sh /dev/sdX1
# 4c. (valgfri) ekstra pakker i rootfs — rediger EXTRA_PACKAGES-listen i scriptet
#     (pt. kun lxterminal) og genkør det ved behov; idempotent:
sudo devuan/extra_packages.sh
# 5. Boks i loader-tilstand, parameter på:
sudo Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool DI -p devuan/parameter_myinit.txt
# 6. SD i boksen, strøm på. SSH: nøglen i devuan/authorized_keys (lægges ind af 06)
```

Efter første boot: `passwd`, `passwd kristian`. Wifi-credentials: med 08 kørt
tilføjes netværk via nm-applet/`nmtui`; ellers manuelt via
`wpa_passphrase "SSID" "kode" > /etc/wpa_supplicant/wpa_supplicant.conf`.

Gendan Lubuntu-boot: `sudo devuan/04_restore_param.sh` (eller fuld `UF`).

## 8. Hvad der ikke virker / videre arbejde

- OpenSSH-server (seccomp vs. 3.10) — brug dropbear
- systemd-sysusers på boksen — divert'ed; pakkeinstallation foregår bedst i qemu-chroot på PC'en
- WebGL i firefox-esr — virker nu (2.0, PowerVR G6110) via hybris +
  `eglplatform_x11` (§5.15c) med Basic-kompositoren; **fulde spil fryser dog
  præsentationen til skærmen** — GL-layers-stien løser render-livelocket i
  vinduet, men skærmen opdaterer ikke (og spil-kørsler har taget boksen ned
  to gange). Status og næste skridt: §5.15d + §5.15e (GPU-MMU-fault fundet)
- Hardware video-decode/GPU-acceleration — WebGL 2.0 virker i firefox-esr via
  hybris-stakken (§5.15c); spil-præsentationen er det åbne spor (§5.15d).
  Løsningsanalyse og rækkefølge: `BROWSER-VEJE.md`
- Mainline-kernel-sporet (Spor B) — parkeret; kun headless-server potentiale
- `reboot` slukker — brug strøm-cykling

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
  beviskæde og fældeliste: **DEBUG-SORT-SKAERM.md**.

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
