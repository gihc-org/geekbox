# Devuan på boksen

## 2. Sådan hænger boksen sammen

```
BootROM (indbygget i chippen, kan ikke slettes)
  → IDB-loader           starter hukommelsen op
  → U-Boot               læser "parameter" og henter kernen
  → parameter            en tekstblok med bl.a. root=/dev/mmcblk0p6 og init=/root/myinit.sh
  → kernel 3.10          starter, initramfs finder rodfilsystemet
  → /root/myinit.sh      VORES script: rydder op, sætter netværk op, udvider disken
  → /sbin/init           sysvinit starter tjenester (netværk, rsyslog, chrony …)
  → nodm                 logger ind som "kristian" og starter X
  → X + LXDE             skrivebordet
```

To detaljer der er værd at kende:

**Imagets rootfs er låst til 1408 MiB.** Vi bygger nyt system ind i det gamle image ved at
overskrive præcis den plads originalen brugte. Det gør flashningen sikker — alle andre dele
af imaget er byte-identiske med producentens — men det betyder at rootfs'en ikke kan blive
større. Med firefox er den fyldt 84 %. `09` har en vagt der stopper i god tid.

**Efter flash udvider `myinit.sh` selv filsystemet** til partitionens 15 GB. Det var
tidligere et manuelt trin, og det blev glemt — med grimme følger (fælde 1).

---

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
  Løsningsanalyse og rækkefølge: `docs/grafik/firefox-webgl.md`
- Mainline-kernel-sporet (Spor B) — parkeret; kun headless-server potentiale
- `reboot` slukker — brug strøm-cykling
