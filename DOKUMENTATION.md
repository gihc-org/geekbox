# GeekBox (RK3368): Fra støvsamler til moderne Linux — den fulde historie

Dette dokument beskriver hele forløbet: hvordan 10 GeekBox-bokse fra 2015-16 fik nyt liv
med Devuan Excalibur (Debian 13-base), hvilke beslutninger der blev truffet undervejs,
hvad der fejlede, og hvorfor. Målet er, at læseren kan forklare løsningen videre — og
genskabe SD-kortet fra bunden.

---

## 1. Udgangspunktet

- **Hardware:** GeekBox, Rockchip RK3368 (8×Cortex-A53 arm64, 2 GB RAM, eMMC, microSD,
  HDMI, Ethernet, WiFi AP6354/BCM4354, GPU PowerVR SGX6110)
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

Alternativet (Spor B, mainline kernel) er parkeret: mainline understøtter hverken
HDMI, GPU eller WiFi på RK3368 — kun en headless server ville være realistisk.

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
ifupdown, fsck) → rc2 (chrony, **nodm**) → X fbdev → LXDE som `kristian`.

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
sudo devuan/07_desktop_audio.sh
# 5. Boks i loader-tilstand, parameter på:
sudo Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool DI -p devuan/parameter_myinit.txt
# 6. SD i boksen, strøm på. SSH: nøglen i devuan/authorized_keys (lægges ind af 06)
```

Efter første boot: `passwd`, `passwd kristian`, WiFi-credentials via
`wpa_passphrase "SSID" "kode" > /etc/wpa_supplicant/wpa_supplicant.conf`.

Gendan Lubuntu-boot: `sudo devuan/04_restore_param.sh` (eller fuld `UF`).

## 8. Hvad der ikke virker / videre arbejde

- OpenSSH-server (seccomp vs. 3.10) — brug dropbear
- systemd-sysusers på boksen — divert'ed; pakkeinstallation foregår bedst i qemu-chroot på PC'en
- Hardware video-decode/GPU-acceleration (PowerVR-blobs + libhybris) — urørt, lav værdi
- Mainline-kernel-sporet (Spor B) — parkeret; kun headless-server potentiale
- `reboot` slukker — brug strøm-cykling
```
