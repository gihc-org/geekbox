# Firefox/WebGL på boks 1 — session-notat 25. aug 2026

## 1. Status i ét blik

**MØNSTER B ER LØST — WebGL 2.0 ER STABILT MÅLT VIRKENDE (25. aug, NORMAL
kørsel, uden strace) — BÅDE som root OG som almindelig bruger (kristian).**
Hele kæden virker nu: GL 3.1 PowerVR Rogue G6110 i Firefox, WebRender-
hardware (ingen SW-fallback), kompositor præsenterer (1280x948), og
`webgl_test_dump.html` meldte `WEBGL_RESULT OK PowerVR Rogue G6200, or
similar WebGL 2.0` efter 3 tegnede frames. Vinduestitlen blev
`OK PowerVR Rogue G6200, or similar WebGL 2.0 — Mozilla Firefox`, og
fbdump viser WebGL-gradienten på skærmen (0 X-fejl).

De sidste tre blokeringer er alle LØST samme dag (detaljer i §3, §7, §8):
1. Den formodede channel-error-race var dels vores egen `pkill -9 -x
   firefox-esr` (rammer KUN main; børnene har prctl-titler som "GPU Process",
   "file:// Content" — de lukker kanalen og exit(0)-kaskader ved main's død),
   dels `MOZ_GL_SPEW=1`, hvis KHR_debug-callback lammer compositoren.
2. `XPutImage` fejlede BadMatch (request 72) — kompositorvinduet er TrueColor
   depth 32, men vi tegnede et 16-bit XImage med root'ens default-GC → sort
   vindue. Fixet: vinduets egen visual/dybde + dedikeret GC + 32-bit ARGB.
3. Sizewaiten ved surface-creation er skåret fra 2 s til 200 ms (50→5 × 40 ms),
   så main-processens synkrone GPU-IPC ikke når sit reply-timeout.

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
   reel størrelse, så overfladen skabes korrekt fra start. SENERE (samme dag):
   ventetiden er skåret til 200 ms (5 × 40 ms) — de 2 s gav
   `Killing GPU process due to IPC reply timeout` (main's synkrone
   `SendEnsureConnected` nåede sit ~2 s-reply-timeout, mens GPU-processen stod
   i sizewaiten). Den levende `refresh_size()` klarer resten: strace-beviset
   under viser at present #1 sker på 1x1, hvorefter størrelsen opdateres.

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
  md5 `78580702ee8b96693b02704fde9b6dcf` (bygget fra repoets
  `eglplatform_x11.cpp`, §2+§3-fix + 200 ms-sizewait + XPutImage-fix).
  Originalkilde også på `/root/eglplatform_x11.cpp`.
- **Wrapper-patchet** (så chvt "lykkes" uden root): `/opt/hybris/libEGL.so`
  (og `.so.1` + `.so.1.0.0`) — i `chvt()` er de to ioctl-kald (VT_ACTIVATE på
  offset 0x23c0, VT_WAITACTIVE på 0x23e0) erstattet med `00 20 00 bf`
  (`movs r0,#0; nop`), så funktionen returnerer 0 uden at skifte VT. Backup:
  `/root/libEGL_hybris.orig`. md5 efter patch: `de560b862291d4ebf9639d3ae664ba9f`.
  Genanvend hvis /opt/hybris nogensinde overskrives.
- **Enhedstilladelser (udev-regler i /etc/udev/rules.d/, permanente):**
  `/dev/console` (tty-gruppe, 0660), `/dev/pvrsrvkm`, `/dev/ion`,
  `/dev/pvr_sync`, `/dev/video_state` (video-gruppe, 0660) — uden
  `/dev/pvr_sync` frigives overflade-buffere aldrig ("alle buffere er busy").
- Stub-mapper: `/root/glstub/` (KUN libGL-stubs — brug denne til rene kørsler)
  og `/root/egl_trace/` (trace-libEGL + stubs).
- Profil `/root/ffprof` indeholder nu `browser.dom.window.dump.enabled=true`
  og `layers.gpu-process.enabled=true` (sidstnævnte EKSPERIMENTEL — giver
  separat GPU-proces; VIRKER nu, når `MOZ_GL_SPEW` IKKE sættes — se §7).
  Ryd `.parentlock` før hver kørsel (ellers "Open Firefox in Troubleshoot
  Mode?"-dialog).
- **Kristian-opsætning (kørsel som almindelig bruger):** se §11.
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
  /usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
  file:///root/webgl_test_dump.html > /root/ff_webgl.log 2>&1
grep -a WEBGL_RESULT /root/ff_webgl.log
grep -aE "x11ws: (vindue|ændret|present|.*buffer)" /root/ff_webgl.log
```

**Forvent (målt 25. aug 2026, normal kørsel):**
```
x11ws: vindue 0x1e00048 pakket ind (1x1)
x11ws: vindue 0x1e00048 ændret størrelse -> 1280x948
x11ws: present #1 (1x1 ...)
x11ws: present #2 (1280x948 ...)
WEBGL_RESULT OK PowerVR Rogue G6200, or similar WebGL 2.0
```
Titlen skal vise `OK PowerVR Rogue G6200, or similar WebGL 2.0 — Mozilla
Firefox`, og `grep -c "X-fejl"` skal være 0. NB: slutter kørslen med
`Exiting due to channel error.` + hybris display-dans, er det pkill'en af
main (børnene lukker kanalen) — ikke en browser-fejl (§7).

## 7. Den "channel-error-race" — LØST: to målefejl, ikke en browser-race

**Symptomet (målt):** i normale kørsler kom der `Exiting due to channel error.`
efter shader 61-64, siden loadede ikke, titlen forblev "Mozilla Firefox".
Under strace virkede ALT. Det så ud som en timing-race.

**Faktisk årsag nr. 1 — vores egen pkill:** `pkill -9 -x firefox-esr` matcher
KUN main-processen. Børnene hedder "GPU Process", "file:// Content",
"Socket Process", "RDD Process" (prctl-titler), så `-x firefox-esr` rammer
dem ikke. Når main dræbes (midtsvejs-oprydning eller `timeout`), bryder
børnenes kanaler → content printer `Exiting due to channel error.` og
`_exit(0)`, og socket/rdd/gpu følger (målt med exit-hook i `pidtag_shim.c`:
`EXIT C ... _exit(0)` 1-2 ms før de andre, main `exit(0)` 11 ms efter). Hele
kaskaden + hybris display-dans ER altså vores oprydning — ikke en browser-fejl.

**Faktisk årsag nr. 2 — `MOZ_GL_SPEW=1`:** med variablen sat installerer
Firefox en KHR_debug-callback, og compositoren stopper efter shader 64
(ingen present, content idle i poll, siden loader ikke — verificeret med
gdb-backtraces: alle processer ventede). Uden `MOZ_GL_SPEW` kører den
fulde kæde i en NORMAL kørsel, både med separat GPU-proces
(`WEBGL_RESULT OK` fra `G ...`) og uden.

**Biprodukter af eftersøgningen (værktøjer i repoet):**
- `pidtag_shim.c` fik exit/`_exit`-hooks: logger `EXIT <rolle> <pid>| <fn>(<status>)`
  med monotont ms — afgjorde hvem der døde hvornår.
- gdb-attach på de levende processer viste at alle ventede (poll/condvar) —
  intet spin og ingen blokeret sync-send i content.
- `pkill -9 -x firefox-esr` efterlader IKKE kørende børn: de lukker selv
  kanalen og exit(0)-kaskader, som beskrevet ovenfor.

## 8. Den sorte skærm (brugerobservation)

**RODÅRSAG FUNDET OG LØST (25. aug):** Firefox' kompositorvindue
(0x1e00048) er TrueColor **depth 32** (1280x948), mens platformen tegnede et
16-bit XImage (root'en er 16-bit) med root'ens default-GC → hver
`XPutImage` fejlede med `BadMatch (request 72)` → Firefox-vinduet forblev
sort, selvom present/WEBGL kørte (361 X-fejl pr. kørsel). `fb0`-read
returnerer kun 2 073 600 bytes (= 1920×540×2) trods 16 bpp — read'en kapres
ved halvdelen.

**Fix i `eglplatform_x11.cpp`:** `put_image()`/`put_ximage()` bruger nu
vinduets EGEN dybde/visual (XGetWindowAttributes) + en dedikeret GC
(XCreateGC på vinduet); til depth 32 konverteres gralloc-RGBA eksplicit til
32-bit X-pixelrækkefølge (bytes [B,G,R,A], LSBFirst). Verificeret: 0 X-fejl,
og fbdump'en viser WebGL-gradienten i vindueområdet (avg (0,39,58), max
B=255/G=215 i canvas-området).

Den kortvarige sorte skærm EFTER Firefox lukkes er derimod hybris'
display-dans (`chvt 7` → sluk/tænd display-enables → `chvt 11` → `chvt 7`),
der kører ved proces-exit — boksen restituerer selv (VT=7, HDMI enable=1).

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
- **`MOZ_GL_SPEW=1` lammer compositoren** på denne stak (KHR_debug-callback;
  ingen present, siden loader ikke). Kør UDEN variablen.
- **`pkill -9 -x firefox-esr` rammer kun main** — børnene har prctl-titler
  ("GPU Process", "file:// Content", "Socket Process", "RDD Process") og
  lukker kanalen med `Exiting due to channel error.` + `_exit(0)`, når main
  dør. Den besked ved kørslens slutning er oprydningsartefakt, ikke en fejl.
- **Firefox' kompositorvindue er depth 32 TrueColor** — XPutImage skal bruge
  vinduets visual/dybde + egen GC; et 16-bit XImage giver BadMatch og sort
  vindue.
- Ryd `/root/ffprof/.parentlock` før hver kørsel.
- pkill: ALTID `pkill -9 -x firefox-esr` (aldrig `-f firefox` — dræber
  ssh-skallet). Børnene rydder selv op (se ovenfor). Tjek VT (tty7) +
  HDMI-enable efter hvert forsøg.
- glibc-reinstall af firefox-esr fjerner glxtest-patchen (backup
  `/root/glxtest.orig`).

## 10. Næste session — det vigtigste at vide

1. **WebGL 2.0 VIRKER STABILT** i normale kørsler (uden strace): `WEBGL_RESULT
   OK PowerVR Rogue G6200, or similar WebGL 2.0`, `present #2 (1280x948)`,
   korrekt titel, 0 X-fejl, indhold på skærmen.
2. Opskriften (root): stub-libGL i `/root/glstub` + platformmodul md5
   `78580702ee8b96693b02704fde9b6dcf` + `layers.gpu-process.enabled=true` +
   **INGEN `MOZ_GL_SPEW`** + ryd `.parentlock` (§6 har hele kommandoen).
3. `Exiting due to channel error.` ved kørslens slutning = vores pkill af
   main (børnene lukker kanalen) — ikke en fejl.
4. Som almindelig bruger: `firefox-webgl [URL]` (eller desktop-genvejen
   "Firefox WebGL") — se §11.
5. Alle værktøjer er i repoet (`devuan/gpu/eglplatform_x11/`), alle md5'er og
   kommandoer i dette notat.

## 11. Kørsel som almindelig bruger (kristian) — LØST 25. aug 2026

WebGL virker også helt uden root, fra skrivebordet. Opsætning:

- `/usr/local/lib/firefox-webgl/` — verdenslæsbare shims
  (`system_shim.so`, `egl_platform_shim.so`), stub-`libGL.so{,.1}`,
  `webgl_test_dump.html` og `test_client_x11`.
- `/usr/local/bin/firefox-webgl` — launcher (repo:
  `devuan/gpu/eglplatform_x11/start_firefox_webgl.sh`). Bruger
  `/home/kristian/ffprof` (kopi af root-profilen, ejet af kristian) og
  åbner URL'en fra argumentet (default `about:blank`).
- Desktop-genvej "Firefox WebGL" (`firefox-webgl.desktop` i repoet) i
  `/home/kristian/Desktop/` og `~/.local/share/applications/`.
- Enhedstilladelser via udev (§5): console til tty-gruppen; pvrsrvkm, ion,
  pvr_sync, video_state til video-gruppen. `kristian` er i begge grupper.
- Wrapper-patchen (§5) er nødvendig — uden den fejler hybris-init'ets chvt
  med EPERM og EGL-displayet starter ikke. File-caps er IKKE en løsning
  (de sætter AT_SECURE, så LD_PRELOAD/LD_LIBRARY_PATH ignoreres).

**Efter genstart (root, i denne rækkefølge):**
```bash
bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
bash devuan/gpu/eglplatform_x11/patch_driver_minor.sh
bash devuan/gpu/gpu_up.sh          # logd + servicemanager + pvrsrvctl + /dev/graphics
```
(udev-reglerne og wrapper-patchen overlever genstart; Android-bind-mounts'ene
og GPU-init'en gør ikke.)

**Verificeret som kristian (normal kørsel):** `WEBGL_RESULT OK PowerVR Rogue
G6200, or similar WebGL 2.0`, `present #1 (1x1)` → `ændret størrelse ->
1280x948` → `present #2 (1280x948)`, titel OK, 0 X-fejl, 0 "alle buffere er
busy", og fbdump viser WebGL-gradienten. GPU-processen kører som kristian.
