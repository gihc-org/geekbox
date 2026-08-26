# DDK 1.5 på GeekBox — løsningen: 3.10-kernel-rebuild med 1.5-KM

> Skrevet 26. aug 2026 (~04:2x). Svarer på spørgsmålet: hvordan kommer vi fra
> "1.5-UM kræver Android 6.0-bionic" + "dumpets pvrsrvctl er 64-bit" til en
> virkende DDK 1.5? Se også `DDK-PROEVEINSTALLATION-SESSION-NOTAT-2026-08-26.md`
> (målingerne) og `DDK-HANDOVER-2026-08-26.md` (backup/restore-plan).

## Status i ét blik

- **Mål:** `GL_EXT_draw_buffers` (MRT) i shader-kompileren → Subway Surfers i
  firefox-esr. DDK 1.5@3830101's `libglslcompiler.so` har udvidelsen; 1.4's har ikke.
- **Prøveinstallation (26. aug):** 1.5-userspace + Android 6.0-libc hænger boksens
  indbyggede 1.4-æra-KM **hårdt ved EGL-init (2× wedge)**. KM'en er bygget ind i
  kernen (tom `/proc/modules`, `insmod` → "Invalid module format", bootlog
  `Rogue L 0.22` ved 2,7 s) → **kernel-rebuild er den eneste vej til en 1.5-KM.**
- **De to navngivne blokader er let løselige** (shim/patch hhv. 64-bit-runtime fra
  dumpet — nedenfor). De løser dog kun load/init, **ikke** ABI-wedgen mod KM'en.
- **Den samlede løsning:** byg en 3.10-kernel med 1.5-KM-kilden
  (`geekboxzone/mmallow_kernel`, gren `geekbox`, `drivers/gpu/rogue`) indbygget —
  og i samme hug opgraderet til 3.10.108 (DRIVER-PORTERING.md §6: stable-serie =
  ABI-frosset ved politik → dage, ikke år).

## Hvorfor userspace-bytte alene fejlede (målt)

- 1.5-UM er bygget mod 1.5-KM's ioctl/bridge-ABI; boksens kernel har 1.4-æra-KM
  indbygget. Første `eglGetDisplay`/connect → hard lockup (2×, anden gang på rent
  filsystem + 6.0-libc, så filsystem-korruption er afkræftet som årsag).
- KM kan ikke byttes som `.ko`: den er bygget ind, ikke et modul.

## Blokade 1: Android 6.0-bionic (`__register_atfork`)

- 5.1-libc mangler `__register_atfork@LIBC` (har kun `__cxa_atexit`); 1.5-libs'ene
  kræver det.
- **Løsning A (anbefalet):** patche 1.5-libs'ene. Symbolerne er versionerede, så et
  bind-mountet shim-bibliotek der eksporterer `__register_atfork` (proxy til
  `__cxa_atexit`) eller en binary-patch af de få relokeringer er nok. readelf-
  verificeret: 1–2 referencer i `libsrv_um`, `libIMGegl`, `libEGL_POWERVR_ROGUE`
  og `libGLESv2_POWERVR_ROGUE`.
- **Løsning B:** medbring Android 6.0's `libc.so` + `/system/bin/linker` og lad kun
  1.5-libs'ene bruge dem (LD_LIBRARY_PATH / bind-mount). **Erstat IKKE hele
  /system-libc** — 5.1-verdenen kan gå i stykker.
- Pointe: løser kun load-fejlen; wedgen mod KM'en kræver stadig 1.5-KM.

## Blokade 2: dumpets `pvrsrvctl` er 64-bit

- Kun ELF64 i `vendor/bin/pvrsrvctl`; `pvrtld` i dumpet er 32-bit. 1.4-pvrsrvctl
  segfault'er mod 1.5's `libsrv_um` (ABI-mismatch).
- **Løsning A:** kopiér 64-bit-runtime'en fra dumpet og kør
  `/system/vendor/bin/pvrsrvctl` — den finder selv `linker64`:
  - `system/bin/linker64` (273.600 B)
  - `system/lib64/libc.so` (807.824 B)
  - `system/vendor/lib64/libsrv_um.so` (1.151.456 B)
  - `system/vendor/lib64/libsrv_init.so` (137.176 B)
  - `system/vendor/lib64/egl/libGLESv2_POWERVR_ROGUE.so` (1.332.840 B)
  (alle ELF64, verificeret via GitHub raw; tag evt. yderligere 64-bit-
  afhængigheder fra dumpet hvis linkeren klager.)
- **Løsning B:** byg en 32-bit `pvrsrvctl` fra 1.5-KM-kildens userspace-værktøjer.
- Pointe: GLES-stien kan i princippet starte uden pvrsrvctl (testen nåede EGL-init),
  men korrekt init er ryddeligst.

## Den reelle blokade og løsningen: kernel-rebuild med 1.5-KM

- **Kilde:** `https://github.com/geekboxzone/mmallow_kernel`, gren `geekbox`
  (RK3368, Android 6.0, kernel 3.10) — `drivers/gpu/rogue/` er DDK 1.5@3830101-KM-
  kilden (611 PVR/rogue-stier verificeret via git/trees; vermagic
  `3.10.0 SMP preempt mod_unload aarch64` matcher boksens kernel).
- **§6-pointeren:** 3.10.79 → 3.10.108 er en stable-merge (intern ABI frosset ved
  politik) → dage, ikke år. Rockchip patchede kernens egne filer, så merge giver
  konflikter i `mm/` og `arch/arm64/` — kendt kode, ikke redesign. Bonus: Dirty COW
  + tre års stable-fixes.

### To byggestrategier

- **A — hurtig hypotese-test:** nuværende 3.10.79 + boksens `.config`, kun 1.5-KM
  tilføjet indbygget (`CONFIG_PVR_ROGUE=y`). Laveste risiko; bekræfter at 1.5-KM
  fjerner wedgen.
- **B — fuld løsning (anbefalet efter A):** merge til 3.10.108 med 1.5-KM samtidig —
  ét byg, både DDK og sikkerhed.
- Anbefaling: A først (testkernel på SD), B bagefter; vil brugeren kun have ét byg,
  spring direkte til B.

## Trinplan (build-forberedelse — afventer godkendelse)

0. **Miljø:** `sudo apt install gcc-aarch64-linux-gnu crossbuild-essential-arm64
   u-boot-tools device-tree-compiler bc bison flex libssl-dev` + SD-kort til test.
1. **Baseline:** byg nuværende vendor-kernel uændret (samme `.config`; hent fra
   `geekboxzone/lollipop_kernel` gren `geekbox` eller `/proc/config.gz` hvis
   CONFIG_IKCONFIG) og verificér boot på SD — uden verificeret baseline er alt
   gætværk.
2. **Klon 1.5-KM-kilden:** `git clone -b geekbox
   https://github.com/geekboxzone/mmallow_kernel` og verificér versionen
   (`strings drivers/gpu/rogue/... | rg "1.5@3830101"` / PVR_BUILD_ID).
3. **Sammensæt bygget:** læg `drivers/gpu/rogue` ind, sæt `CONFIG_PVR_ROGUE=y`
   (indbygget — modul er muligt, men kernen siger "Module unloading is not
   supported", så indbygget er sikrest).
4. **Byg:** `Image` + `rk3368-geekbox.dtb`; behold boksens boot-parametre
   (`cma=128M`, `root=/dev/mmcblk0p6`, `init=/root/myinit.sh`).
5. **Test på SD først**; flash til eMMC først når SD-boot er verificeret. Restore:
   gammelt `update.img` + `/root/system.img.1.4.bak` + `/root/ddk14-backup`.
6. **1.5-UM:** læg 32-bit-sættet fra dumpet i /system (md5 i handoveren), løs
   blokade 1 (shim/patch) og 2 (64-bit-runtime). `/system`-imaget er 254 MB med
   48 MB fri — forstørr (`truncate` + `resize2fs`) hvis nødvendigt.
7. **Verificér:** `shader_ext_test` accepterer `GL_EXT_draw_buffers` → `trivial_test`
   → `egl_display_probe` → Firefox: `WEBGL_RESULT OK` + about:support
   "OpenGL ES 3.1 build 1.5@3830101".

## Risici og fælder

- Kernel-flash kan efterlade boksen uden skærm/ssh uden seriel adapter → **SD-først-
  reglen**; Mask ROM + `upgrade_tool` v1.23 redder altid (README.md).
- Merge-konflikter i `mm/`/`arch/arm64/` er kendt kode — ikke redesign, men tag tid.
- Erstat IKKE hele /system-libc med 6.0's — begræns til shim/patch.
- 64-bit-runtime til pvrsrvctl fylder ~2,7 MB i /system — mål plads først.
- Sikkerhedsregler (målt, gælder hele tiden): dmesg-rotation → beviser gemmes
  straks; aldrig `MOZ_GL_SPEW=1`/XGetImage(root)/`dd if=/dev/fb0` under load
  (fb-read-wedge); `pkill -9 -x firefox-esr` (aldrig `-f firefox`); genstart efter
  ~8 Firefox-opstarter.

## Kilder

- KM: `https://github.com/geekboxzone/mmallow_kernel` (gren `geekbox`,
  `drivers/gpu/rogue`)
- 1.5-UM + 64-bit-runtime: `leddaz-dump-stash/android_rk3368_box_dump`, gren
  `rk3368_box-userdebug-6.0.1-MXC89K-user.root.20181130.004438-test-keys`
  (raw-mønster: `https://raw.githubusercontent.com/leddaz-dump-stash/android_rk3368_box_dump/<BR>/<sti>`)
- 1.4-reference/restore: `geekboxzone/mmallow_vendor_rockchip_common` gren `geekbox`
- DRIVER-PORTERING.md §6 (stable-by-policy), DOKUMENTATION.md §5.15e, TODO.md,
  HAANDBOG.md fælde 16.

## God start i en ny session

> Læs `devuan/gpu/DDK15-KERNEL-REBUILD-LØSNING-2026-08-26.md` (løsningen),
> `devuan/gpu/DDK-PROEVEINSTALLATION-SESSION-NOTAT-2026-08-26.md` (målingerne) og
> `DRIVER-PORTERING.md` §6 og fortsæt derfra. Mål: byg en 3.10-kernel med DDK 1.5-KM
> indbygget fra `geekboxzone/mmallow_kernel` (gren `geekbox`, `drivers/gpu/rogue` =
> 1.5@3830101), så 1.5-userspace kan køre på boks 1 (192.168.0.188) uden ABI-wedge —
> helst i samme hug opgraderet til 3.10.108. Status: prøveinstallationen af 1.5-UM
> er rullet tilbage; boksen er i kendt god 1.4-tilstand; KM'en er bygget ind i kernen
> (ikke .ko), og de to blokader (`__register_atfork` og 64-bit `pvrsrvctl`) løses med
> hhv. shim/patch og dumpets 64-bit-runtime. Plan: (1) find boksen med
> `devuan/find_box.sh` og verificér 1.4-baseline (`shader_ext_test` =
> "Extension not supported"); (2) klargør aarch64-krydskompileren på laptoppen;
> (3) klon `mmallow_kernel` gren `geekbox` og verificér `drivers/gpu/rogue` =
> 1.5@3830101; (4) byg en uændret baseline-kernel (samme .config) og verificér boot
> på SD; (5) byg testkernel med `CONFIG_PVR_ROGUE=y` og behold `cma=128M` +
> `init=/root/myinit.sh` i parameteren; (6) test på SD først, flash til eMMC først
> når SD-boot er verificeret; (7) læg 1.5-UM ind + løs de to blokader, og verificér
> med `shader_ext_test`/`trivial_test` (draw_buffers OK) og derefter Firefox WebGL
> (about:support skal vise "OpenGL ES 3.1 build 1.5@3830101"). Fælder: Mask ROM +
> `upgrade_tool` v1.23 er altid redningsvejen; erstat ikke hele /system-libc; tjek
> /system-plads før kopiering; `pkill -9 -x firefox-esr` (aldrig `-f`); gem
> dmesg-beviser straks.
