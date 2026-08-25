# GPU-fault-gennembrud — session-notat 26. aug 2026 (morgen)

## Aftaler og beslutninger

- [udført] Boks ryddet (run G's Firefox dræbt, VT=tty7, HDMI=1) før nye kørsler (00:1x).
- [målt] **Precache-kørsler (3 stk., 00:17–00:44): `gfx.webrender.precache-shaders=true`
  gør shader-fejlen DETERMINISTISK:** `cs_border_segment` kompilerer Success ved
  normal opstart, men fejler ved `wr_shaders_resume_warmup` ("Compile failed.")
  → reset ved present #50–#150 (~1–3 min). Uden precache (baseline, run G + nye
  baseline-kørsler): samme shader kompilerer Success og reset er flaky (sent/aldrig).
- [målt] **GENNEMBRUD — GPU-MMU-fault i dmesg (tidligere "dmesg tavs" er forkert):**
  `PVR_K: Recovery 1: PID=2116 ... Innocent Lockup` + `BIF0 - FAULT: MMU status
  (0x0000000000007001), Request (0x00040E0102FF9540): TPUA_USC reading from
  0x0102FF9540` + `FW logged fault using PC Address: 0x5FBB4000`. GPU'en læser
  fra en UMAPPET hukommelsesadresse → driveren recover (ødelægger konteksten) →
  WR_POST_UPDATE-reset i userspace. Dette er en RIGTIG GPU-fejl, ikke en
  userspace-falsk-positiv.
- [målt] **gdb-fangst (vedhæftet GPU-processen, vendor-adresse-breakpoints):**
  glGetError ramt 495× (GPU-processens Renderer-tråd), men glCompileShader/
  eglMakeCurrent ramt 0× — Firefox' GPU-proces kalder IKKE vendor-
  libGLESv2's eksporterede glCompileShader direkte; glGetError-breakpointet
  ramte dog (resolveret gennem gdb). Efter reset: **SIGSEGV i Renderer-tråden
  (0x00000000)** — den genstartede GPU-proces crasher.
- [målt] dmesg-ringen fyldes med syscall-403-flood (SoftwareVsyncThread) på
  få minutter — PVR_K-beviset forsvandt fra dmesg inden vi nåede at gemme det
  (skal fanges straks efter reset; gemt her i notatet + i analyse).
- [udført] Boksen genstartet (sikkerhedsregel ~8 opstarter), GPU-stakken
  reinitialiseret (patch_android_bindapi + patch_driver_minor + gpu_up.sh),
  værktøjer genopbygget, gl_reset_probe ren (30 frames, 0 afvigelser).
- [udført] Fangstværktøjer bygget: `gl_capture_shim.c` (LD_PRELOAD; virker
  standalone, men interceptede IKKE Firefox' GPU-proces), `gdb_wr_reset.cmd`
  + `capture_gdb_gpu.sh` (gdb med adresse-breakpoints på vendor-libs).
- [målt] **Buffer-fix (retire-alle + forsinket frigørelse, md5 4ba7a90d) er
  installeret og giver markant bedre stabilitet:** stress-kørsel uden precache
  nåede present #350+ med 0 resets (tidligere: reset ved #2–#150 i ~50 % af
  kørslerne), **0 PVR-faults i dmesg** (tidligere: BIF0-FAULT ved reset) og
  **FPS ~2,7 mod tidligere ~1,5**. eglMakeCurrent-overvågning (gdb, 1469 kald)
  viste 0 fejl-returværdier.
- [udført] eglMakeCurrent-fangst bygget: `capture_egl_makecurrent.sh` (gdb-
  breakpoint på libEGL_POWERVR_ROGUE's eglMakeCurrent offset 0x11d4).
- [målt] **eglMakeCurrent er en skjult flaskehals (26. aug, 01:2x–01:3x):**
  med gdb-breakpoint på vendors eglMakeCurrent (stop/continue pr. kald) kører
  Firefox' kompositor **2,7 FPS** (reproduceret 2×, #100+ uden reset);
  uden breakpoint: **~1,5 FPS**. gdb-attach UDEN breakpoints giver IKKE
  2,7 (1,5) → det er breakpointets stop/continue-effekt på eglMakeCurrent,
  ikke ptrace i sig selv. Forklaring: vendors eglMakeCurrent koster tid pr.
  kald (2–3 kald pr. present); breakpointet returnerer uden at køre koden.
- [målt] LD_PRELOAD kan IKKE intercepte eglMakeCurrent i Firefox' GPU-proces
  (wrapper med EGLSKIP_MAKECURRENT=1: 0 hits — Firefox resolver direkte fra
  vendor-lib'ens handle, dlsym(handle) ser ikke preload-symboler).
- [målt] XSync pr. present er IKKE flaskehalsen: X11WS_NO_SYNC=1 gav stadig
  ~1,6–1,9 FPS (og et reset ved #2) — XSync beholdes (den fanger protokolfejl).
- [målt] Konverteringen (RGBA→ARGB/565) er IKKE flaskehalsen: standalone
  benchmark 1280×948 = ~13 ms (88–97 Mpx/s).

## Status i ét blik

- **WebGL/UI virker; stress-siden fryser efter GPU-proces-reset
  (WR_POST_UPDATE) → sorte frames.** Reset er flaky uden precache (kan komme
  ved #2–#950), men med `gfx.webrender.precache-shaders=true` bliver en
  shader-fejl (`cs_border_segment` ved warmup) + reset deterministisk (~1–3 min).
- **Rodårsagen er nu en MÅLT GPU-MMU-fault:** PVR-kernen melder `BIF0 - FAULT`
  (TPUA_USC læser fra 0x0102FF9540, umappet) → `Innocent Lockup`-recovery →
  kontekst ødelagt → WR_POST_UPDATE. Den genstartede GPU-proces crasher med
  SIGSEGV (gdb-fanget) og tegner sorte frames.
- **Kadence ~1,4 Hz** er fortsat den separate transfer-cap-sag (~2M px/s på
  fuld-vindue-presents), ikke reset'et.

## PVR_K-dmesg-sekvensen (målt 00:41, precache run 5 — bevis fanget før
ringrotation; gdb-log: `devuan/gpu/beviser/gdb_wr_reset-precache5.log`)

```
PVR_K: ------[ RGX summary ]------
PVR_K: RGX BVNC: 5.9.1.46
PVR_K: RGX Power State: ON
PVR_K: BIF0 - OK
PVR_K: RGX FW State: OK (HWRState 0x00000001)
PVR_K: Number of HWR: GP(0/0+0), 2D(0/0+0), TA(0/1+0), 3D(0/1+0), CDM(0/0+0)
PVR_K: DM 2 (HWRflags 0x00000012)
PVR_K:   Recovery 1: PID = 2116, frame = 0, HWRTData = 0x400F1500,
         EventStatus = 0x00000010, CRTimer = 0x000000036CF1, Innocent Lockup
PVR_K:     BIF0 - FAULT:
PVR_K:       * MMU status (0x0000000000007001): PC = 7, Page Size = 0,
                 MMU data type = 0.
PVR_K:       * Request (0x00040E0102FF9540): MCU (128bit word within the Lower
                 256bits, TPUA_USC, Banks 0-3), Reading from 0x0102FF9540.
PVR_K: FW logged fault using PC Address: 0x000000005FBB4000
PVR_K: DM 3 (HWRflags 0x00000012)
PVR_K:   Recovery 1: PID = 0 ... Innocent Lockup (samme request)
```

## Hvad dette betyder

- **Dmesg er IKKE tavs ved reset** — tidligere "dmesg tavs" (run G) var
  måske bare en reset-vej uden fault (flaky), eller fejlen kom senere end
  aflæsningen. Nu er der en konkret kernel-signatur.
- **TPUA_USC (tekstur-PU) læser fra umappet adresse** — klassisk
  use-after-free/for-tidligt frigivet GPU-hukommelse: en tekstur/buffer blev
  unmappet/frigjort mens GPU'en stadig arbejdede med den. Kandidater:
  (a) shim'ens buffer-lifecycle (1×1→resize-dans, retire-fixet venter ikke på
  GPU-færdiggørelse — der er ingen fence på frigørelsesvejen),
  (b) WebRender's teksturcache-eviction (PBO/fence-stien, bug 1773128-mønster).
- **Shader-fejlen med precache er en trigger/indikator, ikke nødvendigvis
  roden** — den gør reset'et deterministisk tidligt, så vi kan fange det.
  cs_border_segment kompilerer Success ved normal opstart, så selve shaderen
  er ikke "i stykker"; fejlen ved warmup kan være en konsekvens af at
  driveren allerede er i dårlig stand.

## Næste skridt (i rækkefølge)

1. **Test det RIGTIGE spil (Subway Surfers) med buffer-fixet** — stress-siden
   har vist #350+ uden reset og PVR-faulten er væk; FPS ~1,5 (2,7 kun med
   gdb-breakpoint, ikke brugbart i drift). Hvis spillet er stabilt, er sagen i
   mål; ellers auto-genstart-workaround som sikkerhedsnet.
2. **Optimering (åbent spor):** eglMakeCurrent koster ~2× pr. present — veje
   videre hvis FPS skal op: (a) patche vendor-eglMakeCurrent (IMGeglMakeCurrent)
   til at springe GPU-synkronisering over (risikabelt), (b) reducér antallet af
   MakeCurrent-kald i Firefox (prefs/kilde), (c) XShm for at fjerne
   server-side-kopien. XSync og konverteringen er afkræftet som flaskehalse.
3. **Fang dmesg STRAKS efter evt. reset** (ring-roterer på få minutter) —
   tilføj `dmesg -c`-overvågning i start_game.sh.

## Fælder + sikkerhedsregler (målt, overtræd ikke)

- ALDRIG MOZ_GL_SPEW=1 eller body-rød-testen; ingen XGetImage(root)/dd
  if=/dev/fb0 under load (fb-read-wedge → sysrq-b).
- dmesg-ringen fyldes med syscall-403 på få minutter — PVR-bevis skal gemmes
  med det samme efter reset.
- Genstart boksen efter ~8 Firefox-opstarter (målt: glxtest i D-state →
  FAIL_NO_CONTEXT/Exhausted GL driver options; kørslerne forringes).
- Efter genstart: patch_android_bindapi.sh + patch_driver_minor.sh (laptop) +
  sh /root/gpu_up.sh (boks) + genopbyg /tmp-værktøjer (scp + gcc; gl_reset_probe
  skal linkes med -Wl,-rpath-link,/opt/hybris).

## Nøglekommandoer

```bash
# fangstkørsel med precache + gdb (deterministisk reset ~1–3 min):
# (precache-pref skal stå i /home/kristian/ffprof/prefs.js)
nohup bash /tmp/capture_gdb_gpu.sh >/tmp/capture_gdb_launch.log 2>&1 &
env GAME_URL="file:///tmp/webgl_stress.html?scale=1&tiles=32&tex=0" \
    GAME_LOG=/tmp/ff_precache5.log RUST_LOG=webrender=debug \
    GL_CAPTURE_SHIM=/tmp/gl_capture_shim.so GL_CAPTURE_LOG=/tmp/gl_capture.log \
    nohup bash /tmp/start_game.sh >/tmp/ff_launch.log 2>&1 &

# aflæs (ingen X/fb-læsninger!):
grep -aE "cs_border_segment|DeviceReset" /tmp/ff_precache5.log | head
grep -a "glGetError\|SIGSEGV" /tmp/gdb_wr_reset.log | tail
dmesg | grep -E "PVR_K|BIF0|Recovery|fault"   # STRAKS efter reset

# gdb-cmd genereres af capture_gdb_gpu.sh (konkrete adresser);
# breakpoints: glCompileShader (GLES2+0x3895c), eglMakeCurrent (EGL+0x11d4),
# glGetError (GLES2+0x2308c)
```

## Repo-tilstand

- Nye filer: `devuan/gpu/eglplatform_x11/gl_capture_shim.c`,
  `gdb_wr_reset.cmd`, `capture_gdb_gpu.sh`; beviser:
  `devuan/gpu/beviser/ff_precache5-2026-08-26.log`,
  `gdb_wr_reset-precache5.log`.
- Uafsluttet fra tidligere: `dmesg-mmc/` (uvist indhold — tjek før commit),
  notat-opdateringer fra natten (allerede commitet i 52e373c).
