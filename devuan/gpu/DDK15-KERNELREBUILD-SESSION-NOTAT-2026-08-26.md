# DDK 1.5 kernel-rebuild — session-notat 26. aug 2026

## Aftaler og beslutninger

- [aftalt] **Brugeren har givet go til build-planen (26. aug ~09:5x):** find/verificér
  boks 1 (1.4-baseline), klargør aarch64-krydskompiler, klon `mmallow_kernel` gren
  `geekbox`, byg uændret baseline-kernel → SD-boot, byg testkernel med
  `CONFIG_PVR_ROGUE=y` → SD-boot, flash til eMMC, læg 1.5-UM ind + løs blokaderne,
  verificér draw_buffers + Firefox WebGL ("OpenGL ES 3.1 build 1.5@3830101").
- [aftalt] **Strategi A først:** baseline-kernel uændret (samme .config), derefter
  testkernel med 1.5-KM indbygget på nuværende 3.10.79. **3.10.108-merge er fase B**
  (efter A er verificeret på SD) — må ikke blokere A.
- [aftalt] **Brugeren bekræftede "fortsæt efter planen" (26. aug ~10:3x):** 3.10.79
  bygges først (boksens egen kernel, laveste risiko); 3.10.108-merge tages bagefter
  som fase B. mmallow_kernel er desuden 3.10.92 (ikke .108) — der findes ingen færdig
  .108-gren at bygge fra.
- [udført] **Boks 1 fundet (192.168.0.188) og 1.4-baseline verificeret (26. aug
  ~09:5x):** kernel 3.10.0 #168 (2016-01-27), PVRSRV-symboler i kallsyms, tom
  `/proc/modules` (KM indbygget), `shader_ext_test` = "Extension GL_EXT_draw_buffers
  not supported" (ES2+ES3) → kendt god 1.4-tilstand.
- [foreslået] **Toolchain: gcc-10-aarch64-linux-gnu** (3.10-æra-kernel; gcc-13 har
  risiko for -fno-common/nye-Werror-problemer). Fase B kan evt. bruge nyere.
- [målt] **Planens præmis om 1.5-KM-kilde er FORKERT (26. aug ~10:0x):** både
  `geekboxzone/mmallow_kernel` (3.10.92) og `geekboxzone/lollipop_kernel`
  (3.10.79), gren `geekbox`, har `drivers/gpu/rogue/include/pvrversion.h` =
  **1.4@3632228** — ikke 1.5@3830101. Ingen 3830101-forekomster i mmallow-træet
  (git grep). Rockchip opgraderede kun 64-bit-userspace til 1.5 (G6110_64).
- [målt] **1.5-KM findes kun som PRÆBYGGET .ko (26. aug ~10:0x):** identiske filer
  (806.864 B, md5-equal) i leddaz-dumpet (`system/lib/modules/pvrsrvkm.ko`) og
  `mmallow_vendor_rockchip_common/geekbox/gpu/libG6110/G6110_64/lib/modules/`.
  Bygget fra `/work/zxl/project/PVR/Rogue_1.5/rogue_km` (target_aarch64),
  `Rogue_DDK_Android rogueddk 1.5@3830101`, vermagic
  `3.10.0 SMP preempt mod_unload aarch64`, **uden `__versions`-sektion**
  (CONFIG_MODVERSIONS var OFF → ingen CRC-tjek ved load).
- [målt] **Boksens kernel KAN loade moduler med matchende vermagic (26. aug
  ~10:07):** `insmod` af 1.4-ko'en fejler KUN med "exports duplicate symbol
  PVRSRVCheckStatus (owned by kernel)" — dvs. vermagic matcher (SMP/preempt/
  mod_unload/aarch64/3.10.0); blokaden er den INDBYGGEDE 1.4-KM, ikke modul-støtten.
  → En genbygget kerne UDEN indbygget PVR kan loade 1.5-.ko'en som modul.
- [målt] **1.5-KM-kildefindbarhed:** `ayufan-repos/rock64/linux-kernel` commit
  462a18f har `drivers/gpu/rogue_m` = 1.5@3830101, men det er **kernel 4.4.83**
  (ligesom Firefly 4.4.55 "Merge 1.5_ED3830101") → port til 3.10 er dagevis arbejde.
- [foreslået] **Korrigeret hovedvej (afventer brugerens go):** byg lollipop_kernel
  (3.10.79, boksens egen kilde) fra `geekbox_defconfig` med `CONFIG_POWERVR_ROGUE`
  slået FRA (ikke indbygget) → boot på SD → `insmod` 1.5-.ko'en → 1.5-UM. Samme
  slutresultat (1.5-KM + 1.5-UM uden ABI-wedge), meget mindre arbejde end at porte
  4.4-kilde tilbage til 3.10. "Indbygget fra kilde" er ikke muligt uden port.
- [foreslået] **Toolchain-justering: gcc-9-aarch64-linux-gnu** (ikke gcc-13):
  -fno-common-default fra GCC 10+ er en kendt fejlkilde for 3.10. Brugeren
  installerer selv via sudo apt (26. aug ~10:1x).
- [målt] **3.10.79 + gcc-9/binutils-2.42 build-workarounds (26. aug ~10:2x–10:4x):**
  (a) `scripts/gcc-wrapper.py` fjernes (python2 + streng -Werror-politik);
  (b) `include/linux/compiler-gcc9.h` kopieres fra compiler-gcc5.h (3.10 kender kun
  gcc3/4/5); (c) dtc shipped-filer: `YYLTYPE yylloc` duplikat → lexer patchet til
  extern; (d) `proc.S` `.section "#alloc,#execinstr"` → `"ax"` (binutils 2.42 afviser
  både #- og %-form); (e) rtl8188eu `extern __inline` → `static __inline`
  (multiple definition med gcc-9). Bygge-script: `devuan/gpu/kernelbuild/build_kernel.sh`.
- [udført] **Baseline-kernel bygget og pakket (26. aug ~10:50):** lollipop_kernel
  geekbox_defconfig (uændret), Image 14.632.768 B, vermagic
  `3.10.0 SMP preempt mod_unload aarch64` (matcher 1.5-.ko præcis), POWERVR_ROGUE=y
  (1.4 indbygget). Pakket som `ramfs-baseline.img` (29,8 MB < 32 MB boot-partition)
  med boksens NUVÆRENDE ramdisk + second (DTB) — verificeret identisk med originalen
  (disp-policy=2), dvs. ingen DTB-ændring. Artefakter: `devuan/gpu/kernelbuild/out/`.
- [udført] **Rollback-sæt taget (26. aug ~11:02):** boksens nuværende boot
  (`/dev/mmcblk0p4` → ramfs_current.img), resource (p3) og parameter (sektor 0,
  64 sektorer) gemt på boksen `/root/kernel-test-backup/` + kopi på laptoppen
  `devuan/gpu/kernelbuild/out/backup/`.
- [afventer] **SD-kort-klargøring (26. aug ~11:0x):** SD-kortet (14,6 GB,
  `/dev/sda`) indeholdt en GAMMEL rootfs uden GPU-stak (ingen /opt/hybris,
  ingen system.img) — skal erstattes med boksens nuværende p6-root (5,1 GB).
  Kræver sudo → brugeren kører kommandoerne selv.
- [foreslået] **Flash-rækkefølge (når SD er klar):** (1) SD i boksen; (2) boksen i
  loader-tilstand; (3) `DI -p parameter_sdroot_myinit_cma.txt` (root=/dev/mmcblk1p1
  + init=/root/myinit.sh + cma=128M); (4) `DI -b ramfs-baseline.img`; (5) boot +
  verificér (uname, ssh, shader_ext_test = "not supported"). Rollback: `DI -b`
  med `out/backup/ramfs_current.img` + `DI -p` med gammel parameter (eller UF).

## Status i ét blik

- Boks 1 er i kendt god 1.4-tilstand (baseline bekræftet 26. aug).
- Krydskompiler og kernel-kilde: under klargøring (step 2-3).
- Byg: baseline-kernel → SD-boot → testkernel (CONFIG_PVR_ROGUE=y) → SD-boot →
  eMMC-flash → 1.5-UM + blokade-fix → verificér.

## Nøglekommandoer / beviser

```bash
# Baseline (boks 1, 26. aug ~09:5x) — bekræftet:
timeout 60 env LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
  EGL_PLATFORM=x11 DISPLAY=:0 /tmp/shader_ext_test
#   ES2: Extension GL_EXT_draw_buffers not supported
#   ES3: Extension GL_EXT_draw_buffers not supported

# Kilder:
git clone --depth 1 -b geekbox https://github.com/geekboxzone/mmallow_kernel /tmp/mmallow_kernel

# Toolchain (noble/universe):
sudo apt install gcc-10-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  u-boot-tools device-tree-compiler bison flex libssl-dev
```

## Næste skridt

1. Klon `mmallow_kernel` (gren `geekbox`) + verificér `drivers/gpu/rogue` =
   1.5@3830101 (PVR_BUILD_ID).
2. Installér krydskompiler + værktøjer.
3. Hent boksens `.config` (`/proc/config.gz` hvis CONFIG_IKCONFIG, ellers fra
   `lollipop_kernel`) og byg baseline-kernel.
