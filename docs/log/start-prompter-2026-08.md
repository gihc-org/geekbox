# Start-prompter — arkiv (august 2026)

Historiske start-prompter, flyttet ud af `TODO.md` 17. september 2026. TODO'en
skal have **én** aktuel prompt — den står i `TODO.md`. Disse beskriver
tilstande der er passeret og skal ikke læses som næste skridt.

De er gengivet ordret. Stierne i dem er skrevet dengang og kan være flyttet
siden — se `README.md` i denne mappe om hvordan loggen læses.

| Dato | Emne |
|---|---|
| 6. sep 2026 | cyan-scene + clone3-kernel |
| 27. aug 2026 | cyan-scene |
| 25. aug 2026 | channel-error-racen |
| 24. aug 2026 | Firefox-WebGL |
| 24. aug 2026 | WebRender-kontekst |
| 24. aug 2026 | M2b-verifikation |
| udateret | Python-frontend + GLES-daemon |

---

## 6. september 2026 — cyan-scene + clone3-kernel

- **God start i en ny session (cyan-scene + clone3-kernel, 6. sep 2026):**
  *"Læs `OVERBLIK.md` (overblik over alle elementer), derefter
  `docs/log/2026-09-06-cyan-clone3.md` (seneste session med
  målinger + beslutninger) og `docs/log/2026-08-27-cyan-scene-handover.md`
  (fuld virkende konfiguration + fælder). Boks 1: IP findes med
  `bash devuan/find_box.sh` (senest 192.168.0.142); kernel kører NU med
  clone3-fix (`3.10.0 #1 SMP PREEMPT Sun Sep 6 22:12:06`); bring-up = insmod
  `pvrsrvkm_leddaz.ko` + `sh gpu_up.sh` + bindapi-lap (sed IP → bash) + tjek
  `/dev/sw_sync` 0666. STATUS: scene-draws sker HELE TIDEN (500–13.000 verts
  pr. kald via glDrawElementsInstanced), men efter-draw-readback viser FBO'et
  ensartet himmel-cyan → store meshes skriver 0 pixels; poki-frit
  replay-probe (scene_replay_probe.c + /root/p7.vs|fs) beviser at spillets
  prog7-shaders + rasterisering VIRKER på 1.5. → fejlen ligger i draw-tilstand/
  data, ikke shader-pipeline. NÆSTE: (1) proxy `715716d0` er installeret og
  logger glDrawBuffers + GL_DRAW_BUFFER0..3 + postdraw — få én vellykket
  load+play og træk `/tmp/cyan_draw_probe.log`; afgør om scenen renderer til
  et andet attachment eller geometrien er uden for frustum. (2) Hvis
  drawBuffers ikke forklarer det: snapshot vertex-buffere via glBufferData/
  glBufferSubData-hook (glGetBufferSubData findes IKKE via eglGetProcAddress)
  og regn clip-space med de loggede matricer. FÆLDER: ingen BiDi-reload under
  load (poki bot=1 → fryser ved 0%); kølepause 5+ min + evt. ren profil ved
  load-stall; readPixels/toDataURL uden for frame er ubrugelig
  (preserveDrawingBuffer=false); proxy v3 med glReadPixels-hook korrelerede
  med load-stall (rul tilbage til v2-funktionalitet); Firefox crasher stadig
  sporadisk — tag målinger hurtigt. Kernel-billede:
  `devuan/gpu/kernelbuild/out/clone3fix/ramfs-clone3fix.img` (flashet;
  rollback: out/test-trace2/…-id.img)."*

## 27. august 2026 — cyan-scene

-  **God start i en ny session (cyan-scene, 27. aug 2026):** *"Læs
  `docs/log/2026-08-27-cyan-scene-handover.md` og
  `docs/log/2026-08-26-gralloc-lock-spor.md` og fortsæt
  derfra. Subway Surfers loader stabilt og er spilbart (lyd/HUD) med
  konfigurationen i handoveren — men 3D-scenen renderer cyan (kun himlen;
  objekter usynlige). Målinger: canvas læser ensartet cyan, clear er pink,
  kun 18-verts-draws, rigtige teksturer uploades, 0 GL/JS-fejl. Find hvorfor
  scene-objekterne ikke tegnes på 1.5-stakken: log 18-verts-draw'ets
  shader/uniformer, tjek kapabilitets-check, sammenlign vertex-shader-
  matematik. Fælder og kommandoer i handoveren."*

## 25. august 2026 — channel-error-racen

  - **God start i en ny session (channel-error-racen, 25. aug 2026):** *"Læs `docs/log/2026-08-25-firefox-webcl.md` (handover med alle spor), `docs/grafik/gpu-historien.md §5.15c, `docs/grafik/firefox-webgl.md` §4 eksperiment 2 og `docs/faeller.md` fælde 23+24. Vi skal have WebGL stabilt i firefox-esr på boks 1 (192.168.0.188). Status: ALT under stakken virker nu — GL 3.1 PowerVR Rogue G6110 i Firefox, WebRender-hardware uden SW-fallback, kompositor præsenterer 1280x948, og WebGL 2.0 er målt virkende (25. aug, under strace): `WEBGL_RESULT OK PowerVR Rogue G6200, or similar WebGL 2.0`. To nødvendige fixes sidder på boksen: stub-`libGL.so`/`libGL.so.1` i `/root/glstub/` (først i LD_LIBRARY_PATH — får Firefox' SymbolLoader til at falde tilbage på eglGetProcAddress i stedet for Mesas dlsym-symboler) og platformmodulet `/usr/local/lib/libhybris/eglplatform_x11.so` (live-vinduesstørrelse + op til 2 s ventetid ved surface-creation — kompositorvinduet frøs på 1x1). ÅBEN blokering: en channel-error-race — i normale kørsler dør content-processen (exit_group 0, ingen signalfejl) med 'Exiting due to channel error.' FØR første present, siden loader ikke og titlen forbliver 'Mozilla Firefox'; under `strace -f` kommer ALT igennem (timing). Desuden: skærmen er sort (brugerobs 24/25. aug) selvom fb0 har mørkt LXDE-indhold — separat display-pipeline-undersøgelse (session-notat §8). Plan: (1) bekræft nuværende tilstand med kommandoerne i session-notat §6 (kør evt. under strace for reference-beviset); (2) find hvem der printer 'Exiting due to channel error' (pid-tag loggen eller strace-tidslinjen) og hvorfor content-processen forsvinder (mistænkt: main-processen laver GPU-arbejde in-process og blokerer content-handshaket — prøv at få separat GPU-proces til at starte rent: `layers.gpu-process.enabled=true` er allerede sat i profilen, men `-gpuprocess` exec'es ikke; tjek GPU-proces-startfejl med vores env); (3) når content overlever: verificér WebGL via `WEBGL_RESULT` i loggen + vinduestitel + `x11ws: present` + fbdump (`cat /dev/fb0 > /root/ff_fb.raw` — NB read'en returnerer kun 2 073 600 bytes trods 16 bpp); (4) opdater `docs/grafik/gpu-historien.md §5.15c, `docs/grafik/firefox-webgl.md` §4, `docs/faeller.md` fælde 23/24 og session-notatet. Fælder: ryd `/root/ffprof/.parentlock` før hver kørsel (ellers Troubleshoot-dialog); `pkill -9 -x firefox-esr` (aldrig `-f`); tjek VT (tty7)/HDMI; gdb crasher på ARM ved LR-retur-breakpoints (brug `egl_trace_lib.c` i stedet); Firefox dlopen'er `libEGL.so` FØR `libEGL.so.1` og `dlsym(handle)` ser IKKE LD_PRELOAD-symboler."*

## 24. august 2026 — Firefox-WebGL

  - **God start i en ny session (Firefox-WebGL, 24. aug 2026) — historisk, planen udført:** *"Læs `devuan/gpu/GLES-DAEMON-PLAN.md` M4a, `docs/grafik/firefox-webgl.md` §4 eksperiment 2 og `docs/grafik/gpu-historien.md §5.15c. Vi skal have WebGL til at virke i firefox-esr på boks 1. Status: `eglplatform_x11` virker for egne GLES-programmer (GLES 3.1 → X-vindue → fb0, ~9 fps), men Firefox' GL-probe (`glxtest`) fejler: den loader vores libEGL + Android-EGL-kæden, og `eglGetDisplay` svarer EGL_BAD_DISPLAY (logd 'eglGetDisplay:218 error 300c') → 'libEGL no display' → fallback til Mesa-software. De samme kald virker i `dlopen_egl_test.cpp`/`egl_display_probe.cpp`. Åben hypotese: glxtest kalder `eglGetDisplay` med sit X-`Display*` (Android-loaderen afviser non-default med 300C). Plan: (1) find boksen med `devuan/find_box.sh`; (2) verificér stakken med `sh /root/gpu_up.sh` + `egl_display_probe` (env: `LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0`); (3) spor glxtest's `eglGetDisplay`-argument med gdb for at bekræfte Display*-hypotesen; (4) tving kaldet gennem hybris-wrapperen (shim-præcedens med RTLD_GLOBAL eller wrapper-patch); (5) når proben siger PowerVR/EGL: kør firefox-esr helt og verificér WebGL via about:support + platformens præsent-log + fbdump. Fælder: hybris' EGL-init skifter aktiv VT (chvt tilbage til X' VT — fælde 19), tegning skal gå gennem vinduets egen X-forbindelse (fælde 20), popen/pgrep fejler i hybris-processer (fælde 21), /proc/cmdline har NUL-argumenter (fælde 22), glxtest rammer Android-loaderens eglGetDisplay (fælde 23). Ryd op efter forsøg: `pkill -9 -f firefox`, tjek VT/HDMI."*

## 24. august 2026 — WebRender-kontekst

  - **God start i en ny session (WebRender-kontekst, 24. aug 2026):** *"Læs `docs/log/2026-08-24-firefox-webcl.md` (handover med alle spor og kommandoer), `docs/grafik/firefox-webgl.md` §4 eksperiment 2, `docs/grafik/gpu-historien.md §5.15c og `docs/faeller.md` fælde 23. Vi skal have WebGL til at virke i firefox-esr på boks 1 (192.168.0.188). Status: glxtest er GRØN — PowerVR Rogue G6110, GLES 3.1, TEST_TYPE=EGL — efter platformens `ws_eglGetProcAddress` videresender kerne-EGL-navne til wrapperen (glxtest henter dem via `eglGetProcAddress`, ikke dlsym) + stubs, og glxtest-binæren er patchet (dybde-tjek 24→16, backup `/root/glxtest.orig`). Fuld Firefox blokerer stadig på WebRender-hardwarekontekst → 'Fallback WR to SW-WR'. To målte fejlmønstre (gdb): 0x300c — `eglBindAPI(ES)` lykkes, men `eglCreateContext` rammer IKKE wrapperen (Android-intern via ikke-kortlagt vej, libepoxy mistænkt); 0x3000 — wrapperens `eglCreateContext` + `eglMakeCurrent` på pbuffer virker, men kontekst-`Init` fejler bagefter. Wrapperens `eglCreateWindowSurface` ramte aldrig (0 hits) — Firefox starter offscreen/pbuffer. Plan: (1) find boksen med `devuan/find_box.sh`; (2) verificér stakken (`sh /root/gpu_up.sh` + `egl_display_probe`, env: `LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0`); (3) byg og kør epoxy-mimic'en (kilde `/tmp/epoxy_mimic.c`, byg på boksen med `-I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm`) — tester libepoxy's EGL-dispatch, som Firefox/libxul linker mod; (4) find mønster-A-kaldets funktionspointer: gdb med break på wrapperens `eglGetProcAddress` (log navne) og evt. på Android-intern `eglCreateContext` (adresse = `/system/lib/libEGL.so`-base + 0x6534, base fra `/proc/<pid>/maps`); (5) diagnosticér mønster-B-`Init` med `MOZ_LOG='GLContext:5'` (kig på `GLContext::InitImpl`); (6) når konteksten virker: kør firefox-esr helt og verificér WebGL via about:support + platformens præsent-log (`x11ws: vindue pakket ind` / `present`) + fbdump. Fælder/noter: ryd op med `pkill -9 -x firefox-esr` (ALDRIG `-f firefox` — dræber SSH-skallet), tjek VT (tty7) og HDMI-enable efter hvert forsøg; gdb: ingen `finish` i kommandoblokke, kun printf+continue; Android-loaderens libs (bionic) er usynlige for gdb; `dlsym_trace.so` er i stykker (brug ikke); en glibc-reinstall af firefox-esr fjerner glxtest-patchen (gen-anvend `/root/glxtest.orig` som reference)."*

## 24. august 2026 — M2b-verifikation

  - **God start i en ny session (M2b-verifikation, 24. aug 2026):** *"Læs `devuan/gpu/README.md` og `devuan/gpu/GLES-DAEMON-PLAN.md` (især M2b). Vi skal gøre M2b færdig: X-vindue-demoen (`window_demo.py` + daemonens `frame`-kommando, rgb565) er implementeret, kører 10 fps og er committed (`2219f6d`) — men den endelige skærm-verifikation mangler: vis at demo-vinduets indhold faktisk når `/dev/fb0`. Boksen findes med `devuan/find_box.sh`. Plan: kør `clearroot` først, start daemon + `window_demo.py` med X kørende, tjek vindue-mapping med `xwininfo -id` midt i kørslen, dump fb0 med `fbdump`. Husk fælderne i planen: ødelæg ikke root-børnevinduer (det slog lxpanel ihjel), tjek aktiv VT mod X' vt, og dræb efterladte daemoner med `pkill -9`."*

## Udateret — Python-frontend + GLES-daemon

  - **God start i en ny session:** *"Læs `devuan/gpu/README.md` og docs/grafik/gpu-historien.md §5.15. Vi skal i gang med Python-frontend + GLES-daemon-projektet — arkitekturen og de første skridt står i README'en og i dette TODO-punkt. Boksen findes med `devuan/find_box.sh`."*
