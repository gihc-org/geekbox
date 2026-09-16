# Boksen — hvad en GeekBox er, og hvorfor den gamle kerne

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
**[grafik/driver-portering.md](grafik/driver-portering.md)**. Samme dokument forklarer, hvorfor
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
