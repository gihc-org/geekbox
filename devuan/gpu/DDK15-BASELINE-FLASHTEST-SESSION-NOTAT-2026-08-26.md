# DDK 1.5 baseline-flashtest — session-notat 26. aug 2026 (fortsættelse)

> Fortsættelse af `DDK15-KERNEL-REBUILD-HANDOVER-2026-08-26.md`. Fokus: baseline
> er flashet; den booter nu forbi U-Boot (blå LED), men fryser stadig. Mål: isolér
> frysepunktet, få baseline til at boote, derefter testkernel + 1.5-KM.

> Agent/model for denne session: [codex:deepseek-v4-flash].

## Aftaler og beslutninger

- [udført] **Baseline flashet med eMMC-parameter (26. aug ~13:0x):** brugeren kørte
  `upgrade_tool DI -p parameter_emmc_myinit_cma.txt` + `DI -b
  out/baseline/ramfs-baseline-id.img` → "Download parameter ok" / "Download image
  ok", strømcyklus efter flash.
- [målt] **Fremskridt vs. sidste session:** tidligere lilla LED (U-Boot-afvisning,
  id=0); nu BLÅ LED → U-Boot-sha-check passerer, kernen starter og når mindst
  device-init/gpio-leds. Men boot fryser stadig; ingen netværkstilstedeværelse
  fundet (scanner: kun 192.168.0.128, ikke boks; 192.168.1.50-fallback heller ikke).
- [målt] **Det flashede billede ER diag-bygget:** `out/baseline/Image` er identisk
  med `out/diag/Image` (md5 `cfe97b8b…`, 12:24) = jan-træ `80f6d15b9d2` +
  marts-defconfig + gcc-9 med `ARM64_CPUIDLE`, `ROCKCHIP_THERMAL` og
  `ROCKCHIP_RK3368_DDR_FREQ` slået FRA (log: `/tmp/kb_build_diag.log`). Fryser
  stadig → DDR-freq/thermal/cpuidle-fra hjælper ikke.
- [afventer] **Skærm-bevis:** hvad viser TV'et ved fryseren? (fbcon/bootlogo/panic?)
  Foto/tekst kan give frysepunktet gratis.
- [målt] **Skærmen viser kun bootlogoet (26. aug ~13:1x).** Kernen har
  `# CONFIG_VT is not set` og ingen fbcon → kernel-beskeder vises ALDRIG på TV'et;
  logoet kan være U-Boot's (resource), hængende mens kernen fryser. Skærmfoto kan
  derfor ikke give frysepunktet.
- [målt] **Ingen seriel adapter tilkoblet** (TODO.md pkt. stadig åbent). DTB bekræfter
  LED-semantik: `gpio-leds` blue default-on → blå LED = gpio-leds prober = kernen
  er i device-init. SMP-bring-up og CMA-opsætning kører FØR initcalls → mindre
  sandsynlige som fryseårsag (men billige at afkræfte).
- [målt] **`maxcpus=1`-test kørt — fryser stadig (26. aug ~13:2x).** Diag-kernel
  (jan-træ, DDR/thermal/cpuidle fra) + `parameter_emmc_diag_maxcpus1.txt` → blå LED,
  samme frys. SMP/PSCI-bring-up er dermed UDELUKKET som årsag (kører før initcalls;
  blå LED viser at initcalls allerede kører).
- [målt] **`out/backup/param_current.bin` er HELT NUL (32 KB nuller)** — backup-parameteren
  blev ikke dumpet korrekt (sector 0-læsning gav nul). `parameter_emmc_myinit_cma.txt`
  er dog identisk med `devuan/parameter_emmc.txt` (09-scriptet bager den ind i
  eMMC-imaget) → den er boksens normale cmdline. Rollback-parameter bør re-dumpes
  når boksen er oppe.
- [målt] **Parameteren ligger på LBA 8192 (sector 8192 = 4 MiB), IKKE sector 0:**
  `PARMH`-magic fundet ved byte 4194304 i eMMC (dd + grep, 26. aug ~13:4x).
  README.md's "sector 0" er forkert/forældet. Korrekt dump:
  `dd if=/dev/mmcblk0 of=param.bin bs=512 skip=8192 count=64`.
- [forkastet] **`reboot loader` virker IKKE på denne boks (målt i kilden, 26. aug
  ~14:0x):** arm64-RK3368 har ingen loader-reboot-håndtering — `arm_pm_restart =
  psci_sys_reset` (arch/arm64/kernel/psci.c:332) ser ikke på cmd-strengen;
  `SYS_LOADER_REBOOT_FLAG + BOOT_LOADER` findes kun i 32-bit
  arch/arm/mach-rockchip/common.c (RK3288 m.fl.). Boksens `reboot` er busybox
  (ingen arg-videresendelse). → Mask ROM (short loader-pins) er fortsat vejen til
  flash; ssh/USB-over-netværk ændrer intet ved det.
- [målt] **`out/control/Image-orig` == `extracted/kernel` (md5
  `570c76a7…`)** — kontrol-billedet indeholder den ægte originale kernel #168.
- [foreslået] **Næste diagnostic (parameter-only, ingen kernel-reflash):**
  (a) `DI -p parameter_emmc_diag_maxcpus1.txt` → isolér PSCI/smp-bring-up;
  (b) `DI -p parameter_emmc_diag_nocma.txt` → isolér CMA/memblock (cma=128M).
- [aftalt] **Testrækkefølge (26. aug ~13:2x):** (1) kontrol-flash
  `out/control/ramfs-control-new.img` (original kernel #168 + vores pakning, id
  `a40f20c6…`, samme parameter) → beviser pakning/parameter/ramdisk end-to-end;
  (2) flash `out/test/ramfs-test-id.img` (PVR FRA + moduler) → hvis den booter er
  indbygget PVR-probe mistænkt, OG vi er direkte på 1.5-vejen (insmod 1.5-.ko);
  fryser den også → fejlen ligger i byg/config/toolchain (ikke PVR) → fortsæt med
  era8 (gcc-8.3 fuld config) / config-bisektion.
- [udført] **Kontrol-flash + parameter-genflash (26. aug ~13:3x):** brugeren flashede
  kontrol-billedet (`DI -b`), men parameteren stod stadig på maxcpus1 → korrigeret:
  `DI -p parameter_emmc_myinit_cma.txt` bagefter (boot-partitionen røres ikke).
- [målt] **KONTROL-BILLEDET BOOTER (26. aug ~13:4x):** original kernel #168 +
  vores pakning + parameter_emmc_myinit_cma.txt → boksen er OPPE (192.168.0.111,
  dropbear). `uname` = 3.10.0 #168 SMP PREEMPT (27. jan 2016), `uname` + full
  bootlog gemt (`/tmp/control-bootlog.txt`). Pakning/parameter/ramdisk er dermed
  beviseligt fejlfri → fryseren ligger i VORES kernel-byg.
- [målt] **Kontrol-boot med `maxcpus=1` stadig i cmdline** (parameteren var ikke
  genflashet endnu) → maxcpus=1 i sig selv hænger ikke; yderligere bevis på at
  SMP ikke er skyld i fryseren.
- [målt] **Original kernel bootlog (reference):** `PVR_K: sys.gpvr.version=Rogue
  L 0.22` @ 2,645 s (1.4-KM prober OK), `leds-gpio` @ 2,98 s (blå LED), bcmdhd
  indbygget (25. jan 2016), stmmac/eth0 OK, ingen panic. `/proc/modules` tomt.
- [målt] **Config-forskel fundet:** marts-defconfig (og diag-byg) har
  `CONFIG_RTL8188EU=y` + `RKWIFI=y` + `WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP=y` og
  INGEN bcmdhd — mens original-kernen har bcmdhd indbygget (wifi på boksen er
  bcmdhd/AP6335-æra). rtl8188eu-probe ved boot er en kandidat til hænger efter
  gpio-leds (blå LED).
- [udført] **shader_ext_test/trivial_test-kilde findes i repoet**
  (`devuan/gpu/eglplatform_x11/{shader_ext_test,trivial_test}.c`) — kan genbygges
  på boksen (gcc, armhf) når den er oppe. 1.5-UM ligger på boksen: `/root/ddk15/`
  (bin/egl/hw/lib + libc-6.0), 1.4-backup i `/root/ddk14-backup/`.
- [målt] **TEST-KERNEN BOOTER (26. aug ~14:1x):** `out/test/ramfs-test-id.img`
  (jan-træ + marts-defconfig + gcc-9.5, PVR FRA + moduler) → boksen oppe
  (192.168.0.146), uname `3.10.0 #1 SMP PREEMPT Wed Aug 26 12:53:41 CEST 2026`,
  `/proc/modules` findes (tom), ingen panic, myinit/eth0/dropbear OK. → **Fryseren
  var den INDBYGGEDE PVR-driver** (1.4 fra jan-træet); med PVR fra booter kernen.
- [målt] **`insmod pvrsrvkm_leddaz.ko` fejler: "Unknown symbol"** (26. aug ~14:2x):
  manglende `trace_buffer_unlock_commit`, `perf_tp_event`, `kmem_cache_alloc_trace`
  m.fl. (12 symboler). 1.5-.ko'en er bygget mod en kernel med ftrace/event-tracing
  PÅ; vores defconfig har `CONFIG_TRACING`/`EVENT_TRACING` FRA (kun `FTRACE=y`).
  Fix: byg med `CONFIG_TRACING=y` (selecter RING_BUFFER/TRACEPOINTS/EVENT_TRACING;
  `kmem_cache_alloc_trace`-eksport er `#ifdef CONFIG_TRACING` i mm/slab.c+slub.c).
- [målt] **Test-kernen mangler VT:** `/proc/tty/drivers` har ingen "vt"; ingen
  /dev/tty0/7. Xorg:0 kan ikke køre nodm-vt7 uden VT → skrivebord kan ikke vises
  på vores kernel. Original-kernen HAR VT (X-loggen "using VT number 7" + virkede
  i morges). Jan-træets egne defconfigs (rockchip_defconfig/defconfig) har OGSÅ VT
  fra → original-kernens config findes ikke i træet (GeekBox-specifik release).
- [målt] **Desktop fejler også på kontrol-kernen (original #168):** Xorg.0.log fra
  kontrol-boot ender ved input-opsætning (83 s), ingen fatal fejl; session dør
  tavst. Årsag fundet: **`S04lightdm` + `S05nodm` begge aktive i rc2.d** (lightdm
  genaktiveret på boksen 19. aug) → to display-managere kæmper om :0.
  TODO.md/DOKUMENTATION siger eksplicit: lightdm virker ikke (logind-seat), nodm er
  DM. Fix på boksen: `update-rc.d -f lightdm remove` (UDFØRT 26. aug ~14:3x, alle
  links væk) + patchet `devuan/07_desktop_audio.sh` så det ikke kommer tilbage.
- [udført] **Genbygning i gang (26. aug ~14:3x):** test-kernel med
  `CONFIG_ENABLE="TRACING VT VT_CONSOLE"` (jan-træ + marts-defconfig + gcc-9.5,
  MODE=test) — build_dir `/tmp/kb_testtrace`, session-log `/tmp/kb_build_testtrace.log`.
  Efter byg: pak med package_bootimg.py → `out/test-trace/ramfs-test-trace-id.img`.
- [målt] **Kernel-bygget er gcc 9.5.0 (Ubuntu 9.5.0-6ubuntu2.1)** — bekræftet via
  `/proc/version` på boksen og strings i Image. Bygget af kristian@laptop 12:53.
- [målt] **`--enable TRACING` virker IKKE alene (promptløs bool droppes af
  olddefconfig).** Løsning: `ENABLE_DEFAULT_TRACERS` (har prompt, selecter TRACING →
  EVENT_TRACING/RING_BUFFER/TRACEPOINTS). arm64 i træet har HAVE_FUNCTION_TRACER,
  men ENABLE_DEFAULT_TRACERS er minimal og tilstrækkelig.
- [udført] **Test-trace-kernel bygget (26. aug ~13:53):** jan-træ + marts-defconfig
  + gcc-9.5 + `CONFIG_ENABLE="ENABLE_DEFAULT_TRACERS VT VT_CONSOLE"` → Image
  15.399.000 B; config bekræftet: TRACING/EVENT_TRACING/RING_BUFFER/TRACEPOINTS/
  VT/VT_CONSOLE=y, POWERVR fra. Alle 9 tidligere manglende symboler findes i
  System.map (tæller 1 hver). Pakket: `out/test-trace/ramfs-test-trace-id.img`
  (30.556.160 B, id `0e65b0a5…`).
- [målt] **1.5-UM-forberedelse:** `/tmp/ddk18` på laptoppen = fuldt 1.5-sæt
  (md5 matcher handoveren, inkl. 64-bit pvrsrvctl `4e4aa6f6…`); `/root/ddk15` på
  boksen = samme 32-bit-sæt. `system_shim.so` eksporterer KUN `system()` — ingen
  `__register_atfork` endnu (skal bygges). linker64/lib64 findes IKKE på boksen
  (skal hentes fra leddaz-dumpet). /system: 48 MB fri — nok.
- [målt] **WiFi er FRAVÆRENDE på vores genbyggede kerner (brugerobserveret 26. aug
  ~15:0x):** NetworkManager har ingen wifi-mulighed. Årsag (målt tidligere): original-
  kernen har `bcmdhd` indbygget (bootlog "Compiled in drivers/net/wireless/bcmdhd
  on Jan 25 2016"); marts-defconfig har `RTL8188EU=y`/`RKWIFI=y` og INGEN bcmdhd →
  wlan0 findes ikke. **Fix: næste kernel-byg skal have bcmdhd slået på**
  (CONFIG_BCMDHD + WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP er allerede y; firmware
  ligger i /system/etc/firmware — kræver også at system.img virker). Tilføjet til
  TODO.md som separat punkt.
- [målt] **system.img er igen utilgængelig (26. aug ~15:0x):** `EXT2-fs (loop0):
  error: ext2_readdir: bad page in #2` + superblock-I/O-fejl; hele /system blev
  ulæseligt efter remount rw + kopiering. e2fsck var ren lige før → billedet er
  skrøbeligt (tidligere korruption/hard-lockup-arvestykke). Genopretning:
  `system.img.1.4.bak` (207.581.184 B = præcis vendor_root-størrelse) kopieres til
  NY fil (nye eMMC-blokke) → e2fsck → mv over system.img → 1.5-geninstallation.
- [målt] **Fælde fundet: dumpets pvrsrvctl + linker64 har mode 644** → exec giver
  "not found" (manglende interpreter / manglende +x). Skal `chmod 755` efter kopi.
- [målt] **Stale loop0 var roden til "tabt" indhold:** efter `mv` af system.img
  læste /dev/loop0 GAMMEL data (dd-md5 afveg fra filen); skrivninger gik til den
  gamle inode og forsvandt. Fix: `losetup` frisk + verifikation at
  `dd if=/dev/loop0` == `dd if=system.img` FØR skrivning. FÆLDE NOTERET.
- [udført] **1.5-UM geninstalleret på frisk system.img (26. aug ~15:1x):** alle
  32-bit-libs + 64-bit-runtime + `libc.so` (6.0) + linker64, md5-verificeret;
  `chmod 755` på pvrsrvctl/linker64.
- [målt] **64-bit pvrsrvctl afhænger af 64-bit libs ud over de 5 i LØSNING-dok:**
  libdl/libm/libcutils/libhardware/libsync/libz/liblog/libc++ (+vendor libIMGegl/
  libusc/libglslcompiler) — alle hentet fra dumpet (system/lib64 + vendor/lib64)
  og installeret.
- [målt] **`gpu_up.sh` → `pvrsrvctl-exit=0` (26. aug ~15:2x):** 1.5-KM + 64-bit
  1.5-pvrsrvctl initialiserer korrekt! Store milepæl — stakken forbinder.
- [målt] **shader_ext_test fejler med "libc.so is not a valid ELF object" fra
  bionic-linkeren (libGLES_trace→libEGL→libGLESv2-kæden):** 6.0-libc (ELF32 ARM,
  md5 512cbcd2) afvises. 6.0-linker (system/bin/linker) også byttet ind (backup i
  ddk14-backup/bin/linker-5.1) — fejlen fortsætter. Intermitterende nul-læsninger
  af libc-6.0.so set (od=nuller, md5 ok efterfølgende) → mistanke om loop/page-
  cache-tilstand. NÆSTE: strømcyklus → ren boot → insmod fra /root-kit →
  gpu_up.sh → shader_ext_test på ren tilstand. Hvis fejlen fortsætter på ren boot:
  Løsning A (atfork-shim + 5.1-libc) i stedet for 6.0-libc-bytte.
- [udført] **Test-kit lagt i /root (overlever strømcyklus):** pvrsrvkm_leddaz.ko
  (8338734f), shader_ext_test, trivial_test, kilder. /system umountet rent.
- [målt] **Rodårsag til "libc.so is not a valid ELF object": boksens libc-6.0.so
  (512cbcd2) var KORRUPT** — arv fra nulstillings-episoden. Frisk download fra
  dumpet (system/lib/libc.so) har md5 `99dcc69f262a4e55440b10293d5c7bc8` (samme
  størrelse 542.192 B). Med den friske libc loader 1.5-kæden korrekt.
- [målt] **DET STORE GENNEMBRUD (26. aug ~14:5x): shader_ext_test =**
  ```
  ES2: compile=OK info=Success.
  ES3: compile=OK info=Success.
  ```
  **GL_EXT_draw_buffers accepteres nu af 1.5-kompileren i både ES2 og ES3.**
  Kæden: test-trace-kernel (PVR fra, TRACING+VT) → insmod pvrsrvkm_leddaz.ko
  (1.5@3830101) → gpu_up.sh (pvrsrvctl-exit=0, 64-bit) → 1.5-UM (32-bit) +
  frisk 6.0-libc + 6.0-linker. Eneste restfejl: "Library 'libPVRDebugger.so not
  found" (valgfri debugger-lib, ikke-fatal).
- [målt] **6.0-libc ægte md5 (reference): `99dcc69f262a4e55440b10293d5c7bc8`** —
  gemt på boksen som /root/libc-6.0-frisk-99dcc69f.so; installeret i
  /system/lib/libc.so.
- [målt] **Uret stod på 2013 (ingen RTC) → HTTPS/TLS fejlede ("certificate is not
  yet valid") → Firefox så "ingen internet".** chrony fik ingen kilder ("No suitable
  source for synchronisation", selv med makestep — sandsynligvis kernel-config
  relateret på vores byg; undersøges senere). Fix: myinit.sh synker nu uret via
  HTTP-Date (port 80 virker) når uret er < 2023. HTTPS = 200 bagefter.
- [målt] **Firefox (bruger kristian) kunne IKKE oprette sockets: EACCES på
  `socket(AF_INET, SOCK_DGRAM)`** — root kunne. Rodårsag:
  `CONFIG_ANDROID_PARANOID_NETWORK=y` (marts-defconfig; af_inet.c:
  `current_has_network()` = `in_egroup_p(AID_INET) || capable(CAP_NET_RAW)`).
  Fix på boksen: `groupadd -g 3003 inet; usermod -aG inet kristian` → DNS +
  Firefox virker (titel "Example Domain — Mozilla Firefox"). **Proper fix: næste
  kernel-byg skal have CONFIG_ANDROID_PARANOID_NETWORK FRA** (+ bcmdhd for wifi).
  07-scriptet skal også oprette inet-gruppen.
- [udført] **bindapi-patchen anvendt (26. aug ~15:1x):** `patch_android_bindapi.sh`
  mod .119 → alle 4 eglBindAPI-kald (inkl. ES3) returnerer TRUE err=0x3000.
  Bind-mount (varer til genstart; køres igen efter reboot).
- [målt] **WEBGL VIRKER I FIREFOX (26. aug ~15:2x):** webgl_test_dump.html →
  `WEBGL_RESULT OK PowerVR Rogue G6200, or similar WebGL 2.0` (WebGL 2.0-kontekst,
  shader kompileret, 3 frames tegnet). cs_blur-WR-shaderfejl er baggrundsstøj
  (blokerer ikke WebGL).
- [målt] **SLUTVERIFIKATION (26. aug ~15:2x):** ny `gl_version_probe.c` viser
  direkte fra 1.5-stakken:
  ```
  GL_VERSION:  OpenGL ES 3.1 build 1.5@3830101
  GL_RENDERER: PowerVR Rogue G6110
  GL_VENDOR:   Imagination Technologies
  ```
  = præcis det about:support skal vise. DDK 1.5-vejen er dermed FULDFØRT:
  test-trace-kernel + 1.5-KM (.ko) + 1.5-UM + pvrsrvctl-exit=0 + draw_buffers OK
  (ES2+ES3) + WebGL OK + versionsstreng bekræftet.
- [udført] **`devuan/gpu/eglplatform_x11/gl_version_probe.c` tilføjet** (repo) —
  genbrugbar verifikation uden at åbne about:support.
- [målt] **EFTER GENSTART (26. aug ~15:3x):** ur-SYNC i myinit kørte men fejlede
  ("Temporary failure in name resolution" — netværk ikke klar endnu) → myinit.sh
  patchet med 5× retry (10 s mellemrum). Uret sat manuelt + HTTPS=200 bagefter.
  GPU-stakken bringes op igen efter hver genstart: `insmod /root/pvrsrvkm_leddaz.ko`
  + `gpu_up.sh` + bindapi-patchen (bind-mount forsvinder ved reboot).
- [målt] **NY FÆLDE: overskrivninger i system.img via loop-mount overlever IKKE
  genstart** (friske filer som linker/libGLESv2 overlevede, men overskrivningen af
  lib/libc.so rullede tilbage til 512cbcd2). Årsag: loop/page-cache-aliasing på
  vendor-kernen. **Robust fix: debugfs direkte i billedet (umount → `debugfs -w -f`
  med `rm`+`write` → e2fsck → mount) — verificeret: libc = 99dcc69f efter reboot.
  Reglen fremover: ved ÆNDRING af eksisterende filer i system.img, brug debugfs;
  ved NYE filer kan loop-rw bruges, men verificér altid md5 efter frisk loop-reattach.**
- [målt] **Efter genstart verificeret (26. aug ~15:4x):** shader_ext_test ES2+ES3
  compile=OK, GL_VERSION "OpenGL ES 3.1 build 1.5@3830101", alle 1.5-nøglefiler
  md5-korrekte i billedet (libIMGegl 077f5079, libsrv_um e175098a, libusc, egl-sæt,
  linker64, pvrsrvctl 4e4aa6f6), inet-gruppen overlevede genstarten.
- [målt] **SUBWAY SURFERS FRYSER STADIG (26. aug ~16:0x) — GL_EXT_frag_depth er
  den resterende blokade:** Firefox-loggen viser præcis:
  ```
  GetShaderInfoLog() -> Compile failed.
  ERROR: 0:2: Extension GL_EXT_frag_depth not supported
  GetShaderSource() ->
  #extension GL_EXT_frag_depth: require
  void main() {}
  JavaScript warning: ... WebGL context was lost.
  ```
  Spillet sender en ES2-stil (ingen #version) kapabilitets-probe; 1.5-kompileren
  afviser udvidelsen → Unity taber konteksten → frys. dmesg ren (ingen GPU-fault).
- [målt] **1.5's frag_depth-status (frag_depth_test.c, 6 varianter):** ES2/ES3 +
  `#extension GL_EXT_frag_depth` + gl_FragDepthEXT = FEJL; ES3-kernens `gl_FragDepth`
  (med #version 300 es) = OK; ES2 gl_FragDepthEXT/gl_FragDepth uden direktiv =
  FEJL (ikke erklærede). → ES2-stien har HVERKEN udvidelsesnavnet eller builtin'et;
  dybde-skrivning findes kun i ES3-kernen. "GL_EXT_frag_depth" findes slet ikke i
  libglslcompiler.so/driveren (kun GL_EXT_draw_buffers). → draw_buffers-fixet (1.5)
  var nødvendigt men ikke tilstrækkeligt for spillet.
- [foreslået] **Sandsynlig byg-forskel hvis kontrol booter:** marts-defconfig
  har `CONFIG_RTL8188EU=y` + `WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP=y`, men boksen
  bruger bcmdhd (wlan0, /system/etc/firmware) → rtl8188eu-probe ved boot er en
  kendt hænge-kilde på Rockchip 3.10.

## Status i ét blik

- Baseline (diag-var. med DDR/thermal/cpuidle fra) er flashet; blå LED, men fryser.
- Billederne ligger klar: `out/control/ramfs-control-new.img` (original kernel),
  `out/test/ramfs-test-id.img` (PVR fra + moduler), rollback
  `out/backup/ramfs_current.img` + `parameter_emmc_myinit_cma.txt`.
- Udført: kontrol booter → test-kernel booter (PVR var skyld i fryseren) →
  insmod-blokade løst (tracing-symboler) → test-trace-kernel bygget + pakket.
- Næste: flash `out/test-trace/ramfs-test-trace-id.img` (+ standard parameter) →
  boot → `insmod /tmp/pvrsrvkm_leddaz.ko` → dmesg `Rogue_DDK_Android rogueddk
  1.5@3830101` → desktop-check (nodm + VT skal give LXDE) → 1.5-UM:
  (a) __register_atfork-shim (udvid system_shim.so), (b) linker64+lib64 fra dumpet
  til 64-bit pvrsrvctl → shader_ext_test draw_buffers → Firefox about:support.
- AFTALT (26. aug ~15:0x): genopret system.img (ny fil + e2fsck + mv) →
  geninstaller 1.5-UM (chmod 755!) → kør gpu_up.sh → shader_ext_test →
  Firefox. pvrsrvctl-exit=0 opnået; shader_ext_test afventer ren boot efter
  strømcyklus. WiFi-regressionen (bcmdhd) tages i samme kernel-byg som eventuelle
  andre config-fix; noteres i TODO.md.
- AFTALT (26. aug ~14:5x): **shader_ext_test = draw_buffers OK (ES2+ES3)** →
  næste: Firefox about:support "OpenGL ES 3.1 build 1.5@3830101" +
  WebGL-verifikation; derefter TODO/DOK-opdatering + commit.
- AFTALT (26. aug ~15:3x): **DDK 1.5-vejen er fuldført og verificeret** (GL_VERSION
  "OpenGL ES 3.1 build 1.5@3830101" + WebGL OK + draw_buffers OK). Restpunkter:
  (a) næste kernel-byg: CONFIG_ANDROID_PARANOID_NETWORK fra + bcmdhd (wifi);
  (b) 07-script: opret inet-gruppe (3003); (c) bindapi-patchen er bind-mount og
  skal genkøres efter reboot; (d) chrony-undersøgelse (NTP afvises på vores byg —
  HTTP-sync i myinit er arbejdsfixet); (e) cs_blur-WR-støj.
- AFTALT (26. aug ~16:1x): **Spillet kræver GL_EXT_frag_depth, som 1.5's ES2-sti
  mangler.** Muligheder til brugeren: (a) shader-omskrivnings-shim (hook
  glShaderSource i GPU-processen; strip direktivet + gl_FragDepthEXT→gl_FragDepth —
  virker kun hvis spillet kører WebGL2/ES3; dages arbejde), (b) Spor B (4.4-kernel
  + DDK 1.8 med frag_depth — uger), (c) acceptér (spillet kræver nyere driver).
  Bevis i `/tmp/ff_poki.log` på boksen; `frag_depth_test.c` i repoet.

## Nøglekommandoer

```bash
cd /home/kristian/projects/geekbox/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23
# Parameter-only-tests (kernel uændret i boot-partitionen):
sudo ./upgrade_tool DI -p /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/parameter_emmc_diag_maxcpus1.txt
sudo ./upgrade_tool DI -p /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/parameter_emmc_diag_nocma.txt
# Kontrol (original kernel, vores pakning):
sudo ./upgrade_tool DI -b /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/out/control/ramfs-control-new.img
# Testkernel (PVR fra + moduler) — 1.5-vejen:
sudo ./upgrade_tool DI -b /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/out/test/ramfs-test-id.img
```

Efter hver parameter-flash: strømcyklus (ikke `reboot`), observer LED + skærm +
`bash devuan/find_box.sh`.
