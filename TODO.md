# TODO: Nyere Linux på GeekBox (RK3368)

## Spor A (aktivt): Devuan Excalibur med vendor-kernel — BOOTER ✅ (aug 2026)

Moderne userspace (Devuan Excalibur/Trixie-base, armhf) på vendor-kernen 3.10.79.
Bevarer HDMI/GPU/WiFi. Boot-strategi: uændret boot-kæde på eMMC, root på SD via
ændret parameter (`DI -p` med tekst-parameter). Scripts i `devuan/`:

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
- [ ] WiFi: wlan0 ses allerede (bcmdhd + firmware fra vendor); konfigurér wpa_supplicant
- [ ] Grafisk miljø: X + LXDE via **fbdev** (/dev/fb0 findes; vendor-Lubuntu brugte fbdev, ikke GPU) — `xserver-xorg-video-fbdev xinit lxde-core lightdm`. Software-rendering, men brugbart på 8×A53
- [ ] Klon SD til de øvrige bokse (dd) — husk parameter-flash pr. boks (03)
- [ ] RTC: boksen har ingen batteri — tid starter i 2013 ved hver boot (apt brokker sig). Installér chrony/ntpsec, eller sæt dato ved netværk i myinit
- [ ] Desktop med GPU: vendor's libhybris-stak (armhf blobs i vendor_root/usr/local/lib) — research, lav prioritet (fbdev dækker det meste)

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
