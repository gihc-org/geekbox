# Gralloc-lock-sporet — session-notat 26. aug 2026 (aften)

> Fortsættelse af `DDK15-1.5-KOMPLET-HANDOVER-2026-08-26.md`. Agent/model:
> [codex:deepseek-v4-flash]. Mål: find EINVAL-kilden i 1.5-gralloc'ens lock
> (brugerens beslutning), så Subway Surfers kan præsenteres.

## Aftaler og beslutninger

- [udført] **RODÅRSAG TIL gralloc-lock EINVAL = /dev/sw_sync-permissions**
  (26. aug ~18:2x): lock=-22 KUN som ikke-root (kristian) — standalone som root
  virkede. Strace viste `openat("/dev/sw_sync") = EACCES` i lock-stien
  (1.5-gralloc laver sync-fence for CPU-låsen via sw_sync; enheden er 0600
  root:root). `chmod 666 /dev/sw_sync` → lock virker som kristian på ALLE
  format/usage-kombinationer (gralloc_test3).
- [udført] **Anden blokade: lock-usage 0x80 gav rc=0 men vaddr=NULL** — x11ws
  var patchet med Android-8-stil GRALLOC_USAGE_SW_READ_OFTEN=0x80, men
  1.5-gralloc'en (Android 5.1) genkender kun 0x3 (SW-bit-maske 0x33) → ingen
  CPU-mapping. x11ws rettet til `X11WS_SW_READ_OFTEN=0x3` → present viser
  faktisk indhold (webgl-test = grøn quad på skærmen, verificeret via
  /dev/fb0-dump).
- [udført] **GPU-crash #1: shader-hook'en læste forbi ikke-NUL-terminerede
  shader-kilder** (WebGL sender eksplicit længde uden NUL) → `strstr`→`memchr`
  crashede (SEGV_ACCERR i libc, PC 0x…6e60 vld1.8). egl_proxy.c gjort
  længde-sikker (contains_sub + fwrite med len) → crash væk.
- [udført] **GPU-crash #2: hybris' _eglXXX-funktionstabel i libEGL_r.so var
  næsten HELT TOM** (kun eglQueryString/eglSwapInterval udfyldt) →
  eglDestroySurface/eglDestroyContext kaldte NULL (crash ip=0x0). Årsag:
  boksens hybris-init udfylder ikke tabellen. FIX: egl_proxy.c's
  `fix_egl_table()` udfylder alle 33 tomme slots via android_dlopen/
  android_dlsym fra /system/lib/libEGL.so (målt: "udfyldte 33 tomme slots").
  → ingen nye eglDestroy-crashes.
- [afventer] **Subway Surfers mister STADIG sin WebGL-kontekst ved
  gameplay-start** (3× "WebGL context was lost" i hver kørsel; spillet viser
  sort canvas + "browser understøtter ikke WebGL"). IKKE forklaret af:
  shader-compile-fejl (0), EGL-fejl (alle makecurrent/swap rc=1 err=0x3000),
  GL-fejl (glGetError-hook: 0 fejl), GPU-crash (fixet), X-fejl, hukommelse.
  Poki-SDK'ens loseContext-probe (bi()) VIRKER (lokal test: kontekst oprettes
  med failIfMajorPerformanceCaveat, loseContext → event, NY kontekst kan
  oprettes). → NÆSTE SKRIDT: fang Firefox' faktiske
  LoseContext-årsag (MOZ_LOG modulnavn i ESR 140 / WebDriver BiDi) eller test
  om rendering i en NY kontekst efter loseContext fejler i 1.5-stakken
  (kontekst-genopretning kan være defekt).
- [foreslået] **Præsentationsbufferens CPU-læsning (pix0) er stale** —
  present #N logger pix0=0 selv når skærmen viser korrekt indhold (hvid
  probe-side / grøn quad). Skærmen vises via direkte scanout, ikke
  XPutImage-kopien; CPU-læsningen er ikke flush-synkroniseret (ingen fence i
  stakken, som tidligere målt). Ikke en fejl for spillet, men loggen kan ikke
  bruges til at afgøre skærmindhold — brug /dev/fb0-dump (fbdump).
- [udført] **Repo synkroniseret med boksen** (26. aug ~19:5x): eglplatform_x11.cpp
  (SELVTEST + timestamps + X11WS_SW_READ_OFTEN=0x3 + sikker retire-livscyklus),
  egl_proxy.c (længde-sikker + eglMakeCurrent/eglSwapBuffers-hooks +
  fix_egl_table + glGetError-hook), gralloc_test3.c, webgl_probe_test.html,
  cdp_ctxloss.py, gralloc_lock_probe.gdb, gpu_segv_probe.gdb.
- [udført] **sw_sync-fixet gjort persistent** (26. aug ~20:0x): chmod 666
  /dev/sw_sync tilføjet til /root/myinit.sh på boksen + devuan/myinit.sh i
  repoet (overlever reboot). Commit b4edb38.
- [målt] **Kontekst-genopretning virker i 1.5-stakken:** efter et bevidst
  loseContext kan en NY WebGL-kontekst oprettes OG rendere (grøn quad,
  readPixels=0,255,0, glError=0; webgl_probe_test.html). → spillets ekstra
  konteksttab (2 ud over SDK-proben) skyldes IKKE manglende genopretning.
- [udført] **STOR KONFIG-FIX: `layers.acceleration.disabled=true`** (26. aug
  ~21:0x): med præsentationen virkende var Firefox' toppanel (chrome) SORT og
  iframe-WebGL-canvasser viste sort. `layers.acceleration.disabled=true`
  (software-layers) får chrome + side til at vise NORMALT (brugerbekræftet) OG
  iframe-WebGL-canvas til at composite korrekt (lokal test: grøn quad i iframe
  med software-layers; sort uden). Prefen er i /home/kristian/ffprof/prefs.js.
- [målt] **Spillet kører i en IFRAME** (5dd312fa….gdn.poki.com). BiDi-preload
  (script.addPreloadScript, da ESR 140 kun har WebDriver BiDi) fangede:
  spillet laver webgl (300x150) → PIXIJS-destroy kalder bevidst loseContext()
  (tom statusMessage) → nye webgl2-kontekster oprettes OK → ingen JS-fejl →
  men spilområdet forsvinder ved gameplay-start. Samme-canvas-getContext efter
  permanent loseContext returnerer NULL (målt lokalt) — Firefox genbruger ikke
  et permanent tabt canvas.
- [målt] **Med software-layers oprettes x11ws-vinduet IKKE** (ingen "vindue
  pakket ind"/presents i loggen; vindue 10x10) — Firefox præsenterer da via
  direkte X-software-compositing, ikke EGL/gralloc-stien. → spilområdet er
  blankt ved gameplay fordi den software-compositede WebGL-canvas-læsning
  fejler efter renderer-overgangen (åbent spor).
- [målt] **readPixels-probe (BiDi-preload): spillets canvas RENDERER korrekt!**
  Spillets rigtige canvas er 836x470 (300x150-konteksterne er prober/små
  canvasser) — readPixels midt i canvas giver CYAN (0,255,255) og
  isContextLost=false gennem hele forløbet. MEN skærmen viser ikke canvas-
  indholdet (kun spredte cyan-spor; skærmen domineres af tapet/sidebaggrund).
  → fejlen ligger i Firefox' software-compositors visning af spillets
  WebGL-canvas (composite/readback-til-X), IKKE i spillets rendering.
- [målt] **Skærmbillede-tidslinje (brugerbekræftet):** ~20-30 s = siden loader
  (spilområde synligt, loading), ~40 s = spilområde sort med "Loading"-tekst,
  ~60 s = spilområdet mangler helt. Resten af siden forbliver.
- [målt] **fbdump(/dev/fb0) er PÅLIDELIG for vindue-regionen** (Xorg bruger
  fb0 med shadow framebuffer) — men vindue kan være 10x10 hvis x11ws ikke
  bruges; tjek xwininfo før konklusion.
- [målt] **GPU-processen crashede igen med eglDestroySurface-NULL-mønsteret**
  under iframe-testene trods fix_egl_table (fixet kørte, men crash i en
  genstartet GPU-proces før første eglCreateContext). Boksen er ustabil efter
  mange kørsler; hele Firefox crashede én gang (ufuldstændig rapport).
- [aftalt] **Nyt spor bagefter: sorte Firefox-chrome (26. aug ~20:1x):** med
  præsentationen virkende er det nu synligt at Firefox' eget toppanel
  (adressefelt, bogmærke-ikon, fanelinje) renderer SORT, mens sideindholdet
  vises korrekt (hvid test-side, farverig Poki-side). Brugeren beskriver det
  som "en mørk film hen over" — også over Poki-logoet øverst på siden. Det er
  browser-UI-rendering (sandsynligvis en accelereret/GL-lags-sti i
  compositoren), SEPARAT fra spillets konteksttab. Køres EFTER
  konteksttabs-sporet er færdigt. TODO + handover opdateret.

## Status i ét blik

- **Præsentation virker NU** (26. aug aften): sw_sync-fixet + usage-0x3-fixet
  gjorde at Firefox-vinduet faktisk viser indhold (webgl-test grøn, probe-side
  hvid). Gralloc-lock EINVAL er løst og verificeret som kristian.
- **Chrome + side viser normalt med `layers.acceleration.disabled=true`**
  (brugerbekræftet) — iframe-WebGL compositerer også korrekt med software-
  layers.
- **Stabilitet:** 2 GPU-crash-klasser fixet (shader-overread, _eglXXX-NULL).
- **Subway Surfers:** loader, men spilområdet forsvinder ved gameplay-start
  (resten af siden forbliver). Åbent spor: software-compositing af spillets
  canvas efter renderer-overgangen (PixiJS-destroy → loseContext → ny
  webgl2-kontekst). **Spillets canvas renderer korrekt (cyan via readPixels) —
  men compositoren viser det ikke → næste skridt: hvorfor Firefox' software-
  compositor ikke viser det 836x470-canvas (nested cross-origin iframe?).
  Værktøjer klar (BiDi-preload med getContext/loseContext/fejl/readPixels-
  logning).**
- Boks: 192.168.0.108; bring-up = insmod + gpu_up + bindapi; sw_sync-chmod
  skal gøres PERSISTENT (udev-rule eller myinit) — IKKE endnu.

## Nøglekommandoer (boksen)

```bash
# sw_sync-fix (midlertidig; gør persistent i myinit/udev):
chmod 666 /dev/sw_sync

# verificér lock som kristian:
runuser -u kristian -- env LD_PRELOAD=/usr/local/lib/firefox-webgl/system_shim.so \
  LD_LIBRARY_PATH=/opt/hybris:/usr/local/lib/firefox-webgl EGL_PLATFORM=x11 \
  /usr/local/bin/gralloc_test3

# EGL-tabelfix i proxy (byg/installer):
gcc -shared -fPIC -Wl,-soname,libEGL.so.1 -o /tmp/libEGL.so.1.0.0 \
  /root/egl_proxy.c -L/opt/hybris -Wl,--no-as-needed -l:libEGL_r.so -ldl
cp /tmp/libEGL.so.1.0.0 /opt/hybris/libEGL.so.1.0.0

# SDK-probe-test lokalt:
#   file:///usr/local/lib/firefox-webgl/webgl_probe_test.html

# skærmindhold (ikke pix0-loggen — den er stale):
/tmp/fbdump /tmp/skaerm.raw
```

## Fælder (nye)

- pix0/pix1 i x11ws-present-loggen er STALE (CPU-læsning uden flush) — brug
  fbdump til at se skærmen.
- EGL-proxyen SKAL bygges med `-Wl,--no-as-needed -l:libEGL_r.so` ellers
  forsvinder libEGL_r fra DT_NEEDED og eglGetDisplay m.fl. bliver ufindelige
  (glxtest fejler, "WebGL creation failed: Exhausted GL drivers").
- WebGL-shader-kilder kan komme UDEN NUL-terminator — brug altid længden i
  hooks (strstr/strlen på rå kilder crasher).
- Firefox 140: `--start-debugging-server` virker ikke; `--remote-debugging-port`
  giver kun WebDriver BiDi (CDP-endpoints = 404).
