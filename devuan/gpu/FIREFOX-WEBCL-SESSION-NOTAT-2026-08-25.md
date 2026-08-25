# Firefox/WebGL på boks 1 — session-notat 25. aug 2026

## 1. Status i ét blik

**MØNSTER B ER LØST — WebGL 2.0 ER MÅLT VIRKENDE (én gang, under strace).**
Hele kæden virker nu: GL 3.1 PowerVR Rogue G6110 i Firefox, WebRender-
hardware (ingen SW-fallback), kompositor præsenterer (1280x948), og
`webgl_test_dump.html` meldte `WEBGL_RESULT OK PowerVR Rogue G6200, or
similar WebGL 2.0` efter 3 tegnede frames.

**Tilbage:** en kør-til-kør-race — i normale kørsler dør content-processen med
`Exiting due to channel error` FØR første present; under strace (alt sænkes
~10x) kommer hele forløbet igennem. Derudover: skærmen er sort (brugerens
observation 24/25. aug) selvom fb0 har (mørkt) skrivebordsindhold — separat
display-pipeline-problem, se §8.

## 2. Rodårsagen til mønster B (InitImpl fejlede med 0x3000) — LØST

Symptomet hele 24. aug: `Failed to create EGLContext!: 0x3000` efter at
`eglCreateContext` + `eglMakeCurrent` (på pbuffer) lykkedes — kontekst-`Init`
fejlede. Hvorfor Init fejlede var ukendt (ingen `eglGetProcAddress`-kald efter
MakeCurrent blev set).

**Beviskæde (25. aug):**
1. `egl_trace_lib.c` (fuld loggende libEGL.so.1, se §4) viste: create →
   pbuffer → MakeCurrent OK → **straks DestroyContext** — og NUL
   `eglGetProcAddress`-kald. InitImpl fejlede altså FØR symbolindlæsningen,
   efter MakeCurrent.
2. Kilde: `SymbolLoader::GetProcAddress` (gecko `GLLibraryLoader.cpp`)
   slår op i `mLib` (dlsym på "libGL.so.1") **FØRST** og kalder først
   `mPfn` (`eglGetProcAddress`) hvis dlsym fejler.
3. Boksens `/lib/arm-linux-gnueabihf/libGL.so.1` er Mesas vendor-dispatch
   (libgl1 1.7.0). Firefox loader den (wrapperens init kalder
   `dlopen("libGL.so")` → Mesa), så **alle** kerne-GL-symboler kom fra Mesa —
   og `fGetError()`/`fGetString()` blev kaldt på Mesa uden Mesa-kontekst →
   Init fejlede stille. Derfor nul eglGetProcAddress-kald: dlsym vandt altid.
4. `firefox_seq_probe.c` (isoleret replika af Firefox' eksakte attribut-lister
   og config 0x12) beviste at create på PowerVR virker i isolation; kun
   khr-robustness-attributter (0x30fc=4) fejler med 0x3004 (driveren), alle
   ext/req-varianter virker. Dermed: Firefox' problem var ALDRIG create —
   det var symbolindlæsningen (Mesa).

**Fix:** stub-`libGL.so`/`libGL.so.1` (tomme .so'er, ingen gl*-eksporter) i
`/root/glstub/` (og `/root/egl_trace/`), lagt FØRST i `LD_LIBRARY_PATH`:
- wrapperens `dlopen("libGL.so")` → stub (Mesa loades ikke ved init), og
- Firefox' `PR_LoadLibrary("libGL.so.1")` → stub → hvert dlsym fejler → fallback
  til `eglGetProcAddress` → wrapper → Android/PowerVR GLES. Samme vej som
  glxtest (som var GRØN).

**Verificeret:** `GL version detected: 310`, `OpenGL vendor: Imagination
Technologies`, `OpenGL renderer: PowerVR Rogue G6110`, `Detected profile:
310 es`, WebRender-shaders kompilerer, ingen "Fallback WR", ingen
"Failed to create EGLContext".

## 3. Andet fund: kompositorvinduet blev frosset på 1x1 — LØST

Firefox opretter kompositorvinduet som 1x1 og resizer det til 1280x948 lige
efter. Android-EGL'en spørger kun størrelsen ÉN gang (ved
`eglCreateWindowSurface`) → EGL-overfladen blev 1x1 og compositoren nåede
aldrig første frame (ingen `x11ws: present`).

**Fix i `devuan/gpu/eglplatform_x11/eglplatform_x11.cpp` (X11NativeWindow):**
1. `refresh_size()` — `width()/height()/defaultWidth()/defaultHeight()` (nu
   `const` med `mutable` medlemmer) og `queueBuffer()` henter den LEVENDE
   X-størrelse via `XGetWindowAttributes`; ændring sætter `m_sizeDirty` og
   `dequeueBuffer()` reallokerer.
2. `x11ws_CreateWindow()` venter op til 2 s (40 ms × 50) på at vinduet får
   reel størrelse, så overfladen skabes korrekt fra start.

**Verificeret (i den vellykkede strace-kørsel):**
```
x11ws: vindue 0x1e00048 pakket ind (1x1)
x11ws: vindue 0x1e00048 ændret størrelse -> 1280x948
x11ws: present #1 (1x1 ...)
x11ws: 2 buffer(e) allokeret (1280x948 ...)
x11ws: present #2 (1280x948 ...)
WEBGL_RESULT OK PowerVR Rogue G6200, or similar WebGL 2.0
```

## 4. Nye værktøjer (alle gemt i `devuan/gpu/eglplatform_x11/`)

| Fil | Formål / bevis |
|---|---|
| `firefox_seq_probe.c` + `build_firefox_seq_probe.sh` | replikerer Firefox' eksakte chooseConfig/bindAPI/create-sekvens (config 0x12, pm_khr/pm_ext/khr/ext/req); beviser create virker isoleret, khr-robustness fejler 0x3004 |
| `egl_trace_lib.c` + `build_egl_trace_lib.sh` | fuld loggende libEGL.so.1 (argumenter + returværdier); afslørede mønster B (create/MakeCurrent OK → Init fejler uden P-kald) og senere den fulde succes-sekvens |
| `egl_ret_trace.c` + `build_ret_trace.sh` | LD_PRELOAD-interposer — NYTTEDE IKKE: Firefox henter symboler via `dlsym(libEGL-handle)`, som foretrækker wrapperens egne eksporter frem for preload (brug `egl_trace_lib` i stedet) |
| `build_stub_gl.sh` | bygger tomme stub-`libGL.so`/`libGL.so.1` med korrekt SONAME (fixet i §2) |
| `run_ff_eglshim.sh` | kører Firefox med trace-lib + stub (LD_LIBRARY_PATH=`/root/egl_trace:/opt/hybris`) |
| `run_ff_webgl_report.sh`, `webgl_test_dump.html` | WebGL-verifikation via `window.dump()` + prefs `browser.dom.window.dump.enabled` |
| `gdb_ff_init.cmd` + `run_ff_gdb_init.sh` | gdb-spor med returværdier — GDB CRASHER på ARM ved LR-retur-breakpoints ("fatal error internal to GDB"); brug i stedet `egl_trace_lib` |
| `run_ff_verify.sh`, `run_ff_retrace.sh` | verifikations-/sporkørsler |

`eglplatform_x11.cpp` er ændret (git-status viser det) med §2- og §3-fixene.

## 5. Boksens tilstand (25. aug 2026, efter oprydning)

- Android-patches sidder stadig (bind-mount, gælder til reboot):
  `/system/lib/libEGL.so` md5 `467debb34a7c38a4494b1942c0b2bec5`
  (bindAPI-normalisering + chooseConfig-ES2-sti), `/system/vendor/lib/libIMGegl.so`
  md5 `aa370d75715e73748f08bc5f91127ab4` (minor2-bhi nop).
  Scripts: `patch_android_bindapi.sh`, `patch_driver_minor.sh`.
- Platformmodul installeret: `/usr/local/lib/libhybris/eglplatform_x11.so`
  md5 `b5dc98455028bdb0857004eded524c38` (bygget fra repoets
  `eglplatform_x11.cpp`, §2+§3-fix). Originalkilde også på `/root/eglplatform_x11.cpp`.
- Stub-mapper: `/root/glstub/` (KUN libGL-stubs — brug denne til rene kørsler)
  og `/root/egl_trace/` (trace-libEGL + stubs).
- Profil `/root/ffprof` indeholder nu `browser.dom.window.dump.enabled=true`
  og `layers.gpu-process.enabled=true` (sidstnævnte EKSPERIMENTEL — giver
  separat GPU-proces; hjælper delvist, se §7). Ryd `.parentlock` før hver kørsel
  (ellers "Open Firefox in Troubleshoot Mode?"-dialog).
- VT=tty7, HDMI=1, ingen Firefox kører.

## 6. Kør-selv (reproducer WebGL-verifikationen)

```bash
# Forudsætning: stubben bygget (build_stub_gl.sh) og platformen installeret
ssh -i /home/kristian/.ssh/geekbox_key root@192.168.0.188

pkill -9 -x firefox-esr; sleep 1
rm -f /root/ffprof/.parentlock /root/ffprof/lock
cd /root
timeout 90 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so" \
  LD_LIBRARY_PATH=/opt/hybris:/root/glstub EGL_PLATFORM=x11 DISPLAY=:0 \
  MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
  MOZ_GL_SPEW=1 \
  /usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
  file:///root/webgl_test_dump.html > /root/ff_webgl.log 2>&1
grep -a WEBGL_RESULT /root/ff_webgl.log
grep -aE "x11ws: (vindue|ændret|present|.*buffer)" /root/ff_webgl.log
```

Under strace (`strace -f ...`) kommer kørslen gennem; i normale kørsler
kommer content-processen ikke i mål (race, §7).

## 7. Tilbageværende blokering: channel-error-racen

**Symptom:** normale kørsler: `Exiting due to channel error.` kort efter
kompositoropstart (1x1-vindue skabt, shaders 61-64 kompileret), content-
processen forsvinder (exit_group(0) — IKKE et signal), siden loader ikke,
titlen forbliver "Mozilla Firefox". Under strace virker ALT (sandsynligvis
fordi timingen ændres — indtil videre det eneste pålidelige gennembrud).

**Målt:**
- Uden stub (gammel env): content virker, siden loader, men WebGL fejler
  (forventet — ingen hardwarekontekst): `WEBGL_RESULT FAIL_NO_CONTEXT`.
- Med stub + `layers.gpu-process.enabled=true`: separat GPU-proces skaber
  overfladen direkte i 1280x948, men to channel errors og stadig ingen present.
- Content-processen (tab) spawner undertiden sent; strace viste den leve med
  ~35 tråde. `dmesg` har kun seccomp-"syscall 403"-støj (lxpanel m.fl.), ingen
  segfault.
- `nspr_use_zone_allocator`-fejl i LD_DEBUG optræder i BÅDE stub og nostub
  (godartet — ingen eksporterer symbolet på boksen).

**Hypoteser (næste skridt, prioriteret):**
1. Main-processen laver GPU-arbejde in-process og blokerer content-
   handshaket → prøv at få GPU-processen til at starte rent (check hvorfor
   `-gpuprocess` ikke exec'es: manglende prefs? fejl i GPU-proces-start med
   vores env?), evt. `MOZ_GPU_PROCESS...`-logs.
2. Undersøg hvem der printer "Exiting due to channel error" (pid i loggen):
   kør med process-rolle-tagget output og sammenlign med strace-tidslinjen.
3. Prøv at forsinke compositorstarten (fx wrapperens size-wait forlænges), så
   content-handshaket når i mål før WebRender-init.
4. Hvis racer: `layers.gpu-process.enabled=true` + `gfx.webrender.enabled=true`
   eksplicit, og tjek `about:support`-værdier via dump.

## 8. Den sorte skærm (brugerobservation)

Brugeren meldte "Skærmen er sort" 24. aug aften. Målt: X kører (fbdev, tty7,
1920x1080x16), HDMI enable=1, fb0 har mørkt LXDE-indhold (næsten-sort tapet —
top-farver 1-5/255), men Firefox-vinduets indhold nåede aldrig fb0 (dumps af
fb0 under Firefox var identiske med baseline). `/dev/fb0`-read returnerer kun
2 073 600 bytes (= 1920×1080×1) trods 16 bpp — read'en kapres ved halvdelen;
kontrollér om HDMI'et viser fb0 eller en anden plane/fb. Brug
`DEBUG-SORT-SKAERM.md`-tjeklisten og `fb_overscan.py --show`.

## 9. Vigtige fælder/noter (tilføjelser til fælde 23-notatet)

- Firefox dlopen'er **`libEGL.so` FØRST**, derefter `libEGL.so.1`
  (GLLibraryEGL) — en trace/libEGL.so.1 i LD_LIBRARY_PATH blev derfor ikke
  brugt; lav begge navne.
- Wrapperens init kalder `dlopen("libGL.so")` (ikke DT_NEEDED) → trækker Mesa
  ind i processen. Stubben skal hedde `libGL.so` OG `libGL.so.1`.
- `dlsym(handle)` (Firefox' EGL-symbolopslag) foretrækker bibliotekets egne
  eksporter frem for LD_PRELOAD → interposer-vejen virker ikke for EGL.
- gdb på ARM: LR-retur-breakpoints (Python `stop()` på `*lr`) får gdb til at
  segfault ("A fatal error internal to GDB"). Brug egl_trace_lib i stedet.
- Content-processen afslutter med exit_group(0) — kig IKKE kun efter signaler.
- `strace -f` ændrer timingen og kan få racen til at forsvinde (og dukke op).
- Ryd `/root/ffprof/.parentlock` før hver kørsel.
- pkill: ALTID `pkill -9 -x firefox-esr` (aldrig `-f firefox` — dræber
  ssh-skallet). Tjek VT (tty7) + HDMI-enable efter hvert forsøg.
- glibc-reinstall af firefox-esr fjerner glxtest-patchen (backup
  `/root/glxtest.orig`).

## 10. Næste session — det vigtigste at vide

1. Stakken ER grøn: GL 3.1 + WebRender-hardware + WebGL 2.0 (målt én gang).
2. De to nødvendige fixes sidder på boksen (stub-libGL + platform-resize).
3. Den eneste tilbageværende blokering er content-processens channel-error-
   race; strace-kørslen er reference-beviset på at alt andet virker.
4. Alle værktøjer er i repoet (`devuan/gpu/eglplatform_x11/`), alle md5'er og
   kommandoer i dette notat.
