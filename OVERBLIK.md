# GeekBox (RK3368) + Linux + Subway Surfers WebGL — overblik

> Oprettet 6. sep 2026 som samlet indgang til projektets mange elementer.
> Læs først: [README.md](README.md) (formål/flash), seneste session
> `docs/log/2026-09-25-cyan-fps.md` (seneste, 25. sep S1),
> `docs/log/2026-08-27-cyan-scene-handover.md` (opgavebaggrund) og
> `docs/log/2026-08-26-gralloc-lock-spor.md` (beslutningslog).

### Status 17. sep 2026 (fps-sporet)

**Opdateret 19. sep 2026 (SLUTSTATUS):** spillet er **spilbart igen** —
brugeren spillede 19. sep og satte det på pause, og spilområdet forblev synligt
hele tiden. Opskriften er den samme som 8. sep: proxy **vnext12 `dcc0a68f`**
(den TUNGE) + alpha-shim via BiDi-preload + `CYAN_DEPTHCLEAR=1`.
**Brug ikke `CYAN_LIGHT=1` når spillet skal ses** — uden de løbende readbacks
frøser præsentationen (0,2 skærm-opdateringer/s mod 1,5). Målt 19. sep på en
triviel side: tung vnext12 = 1,5/s, light = 0,2/s, Firefox uden hybris/EGL =
3,2/s — **NB (20. sep): disse tre tal er målt med 200 ms poll på en
flyt-boks-side og er derfor ikke rater, kun relative signaler (fælde 49);
"light frøser" er stadig gyldig som driftsregel.** Fps er stadig ~2,6 og er det
åbne spor; se "God start"-prompten i `TODO.md`.

**Opdateret 19. sep 2026 ~22:25 (fps-sporet, målt i spillet):** spillet kører
**2,4-2,6 fps** og skærmen opdaterer 2,4-2,6 gange/s (den følger spillet).
Af de ~400 ms pr. frame er kun **~10 ms draw-kald** (~250-280 GL-kald/frame;
driveren alene = 0,099 ms/kald, proxyens egne kald oveni = 0,004 ms/kald) og
23-48 ms spillets egen JS — resten ligger uden for spillet. Bevis: en triviel
CSS-side uden WebGL giver kun **1,1 skærm-opdateringer/s og 5,6 rAF/s** i samme
opsætning. **Loftet er altså browserens compositing/præsentation på denne
stak, ikke spillet, proxyen eller driverens kald-pris.** De tidligere
"~2.000-2.700 kald pr. frame" var en JS-tællerfejl (~8x, rettet).
Display-dansen kan nu slås fra (`SHIM_NO_DISPLAY_DANCE=1 SHIM_NO_CHVT=1` i
`system_shim.c`), så målinger ikke længere fryser X (fælde 47); 720p via
`/sys/class/display/HDMI/mode` alene ødelagde billedet (fælde 48). Detaljer:
`docs/log/2026-09-19-cyan-fps.md`.

**Opdateret 20. sep 2026 ~23:00 (areal-testen kørt):** loftet er
**arealbestemt præsentation**. Målt med en kontrolside der gentegner hele
vinduet hver frame (`raf_test_full.html`, poll hver 20 ms, så hver præsenteret
frame ændrer alle blokke): **1920x1054 = 2,4 skærm-opdateringer/s** mod
**1000x600 = 10,3/s** — 4,4x for 3,4x areal, dvs. ~0,2 µs pr. pixel og
~5-6 Mpx/s gennem software-præsentationen. rAF-linjen viste samme tal, så hver
frame når skærmen. **Spillets 2,4-2,6 fps ved 1080p er derfor
præsentationsloftet (~430 ms/frame), ikke spillet.** Forventet gevinst: 720p
(0,92 Mpx) ~2,2x → ~5 fps; et 1000x600-vindue ~3,4x → ~10 fps.
Tre målefælder blev fundet og rettet undervejs (fælde 49-51): `fb_fps` med
200 ms poll kan ikke måle over ~2-5/s (og den gamle flyt-boks-side målte
opholdstid, ikke rate); `x_focus` blev **aldrig** kørt (`runuser -u kristian`
mod en 0700-`/root` → Permission denied); og et maksimeret vindue kan ikke
skaleres med `XMoveResizeWindow` — størrelsen skal sættes i profilens
`xulstore.json` før start. **17. sep-konklusionen "et mindre vindue hjalp
ikke" er derfor ugyldig.** Detaljer: `docs/log/2026-09-20-cyan-fps.md`.

**Opdateret 25. sep 2026 (bredere analyse: loftet ligger i Firefox'
kompositor):** spillet er målt i tre vinduesstørrelser samme dag — 1920x1054
(tab-crash under kørslen; 19.-20. sep 2,4-2,6/s), 1280x720 (**1,8-1,9/s**,
2 kørsler) og 640x360 (**4,2-4,8/s**). En kontrolside i 1280x720-vinduet gav
6,0/s, så areal-modellen holder for browserens grundflade — men ikke for
spillet, hvis canvas (836x470 i 720p-vinduet, 640x360 i det lille) ikke følger
vinduet monotont. **De nederste lag er målt og er hurtige:** `memcpy` til
`/dev/fb0` tager 3,1 ms for et helt 1080p-frame (1.285 MB/s), X's egen
`XPutImage`-vej 11,4 ms/frame (87,8 fps), og CPU'en ramper 312 -> 1200/1296
MHz under last. Firefox leverer alligevel kun 2,4/s og logger `Over max
pending transaction limit when trying to paint, skipping` — altså er
**browserens kompositor (Basic/software; WebRender er slået fra) flaskehalsen**,
~30-40x langsommere end platformen under den. Afvist ved måling: manglende
write-combining/uncached fb, langsom X-fbdev-vej, compositing manager,
ARGB-konvertering (vinduet er depth 16) og DVFS. 4.4-kernel-sporet er
revurderet: KMS/page-flip løser ikke dette loft (fb/X-vejen er allerede
hurtig); den reelle grund til Spor B er DDK 1.8 (bedre GL-driver til
WebRender/GPU-vejen). Prioriterede nye spor (S1 skift Firefox' kompositor-vej,
S2 profilér kompositoren, S3 canvas-nedskalering via preload, S4 systemhåndtag)
står i **`docs/grafik/fps-analysen-2026-09-25.md`**; dagens målinger i
`docs/log/2026-09-25-cyan-fps.md`.

**Opdateret 25. sep 2026 ~22:10 (S1 kørt — kompositor-vejen er en blindgyde):**
de fire/fem kompositor-varianter blev målt samme aften (kontrolside + spil,
1080p, `POLL=20`) med `devuan/gpu/eglplatform_x11/s1_variants.sh` og
`s1_prefs/V1-V5.user.js`:

| Vej | Kontrolside | Spil (1080p) |
|---|---|---|
| V1 baseline (WebRender fra, software) | 2,6/s | 0,8-1,0 fps |
| V2 SWGL (`webrender.software=true`) | 2,2/s | 0,8-0,9 fps |
| V3 WebRender på GPU | 0,9/s + dødt GPU-barn | ikke kørt |
| V4 software uden GPU-proces | 2,0/s | 0,6-0,9 fps |
| V5 som V3 uden GPU-proces | 1,0/s + sort chrome | ikke kørt |

**Ingen gevinst:** software-vejene ligger inden for støj omkring baselinen, og
WebRender-på-GPU (V3/V5) dør med `DeviceReset DRIVER_ERROR ::WR_POST_UPDATE`
hvorefter browserens chrome aldrig bliver malet (sort skærm med et farvet
felt); livlinen er `service nodm restart`. Dermed er kompositor-vejen udelukket,
og sporet går videre til **S2 (profilering)** og **S3 (canvas-nedskalering)**.
NB: dagens 1080p-spilkørsler ligger på ~1 fps mod 2,4-2,6 19.-20. sep (årsagen
er ikke målt, og `service nodm restart` hjalp ikke); `MOZ_LOG`-modulerne
Compositor/LayerManager/WebRender er tavse i denne build, så vælg vej efter
proces-snapshot i stedet. Fælder: `docs/faeller.md` 53-56.

- **Spillet renderer stadig korrekt** (vnext12 `dcc0a68f` + alpha-shim +
  `CYAN_DEPTHCLEAR=1`). Draw-kaldene er ikke loftet: driveren alene koster
  0,099 ms/kald og spillet ~250-280 kald/frame = ~10 ms/frame; proxyens egne
  kald +0,004 ms/kald. (De gamle "~2.000-2.700 kald pr. frame" var en
  JS-tællerfejl ~8x, og "mindre vindue hjalp ikke" blev målt på et vindue der
  aldrig blev mindre — se fælde 49-51.)
- **`eglSwapBuffers` kaldes ALDRIG** med software-layers → fps skal måles i
  JS-laget (rAF) eller på skærmen (`fb_fps`), ikke i GL-laget.
- **"Spilområdet forsvinder"** = browseren får ikke afleveret frames: to
  skærmdumps med et minuts mellemrum var byte-identiske mens musemarkøren
  bevægede sig fint. Se `docs/faeller.md` fælde 44 (display-dans/X-genstart)
  og 45 (fps-loftet).
- **Nye værktøjer** (i `devuan/gpu/eglplatform_x11/`): `fb_fps.c`,
  `x_focus.c`, `x_resize.c`, `depthclear_probe.c`, `readback_probe.c`,
  `drawbench_probe.c`, `cyan_fps_run.sh`, `raf_test.html`,
  `raf_test_full.html` (fuldt gentegnet kontrolside, 20. sep),
  `kill_bidi.sh`;
  `bidi_ctxloss.py` har `preloadmin` + `SHOT_AT`/`SHOT_POLL` (screenshot fra
  Firefox' egen gengivelse).
- **Proxy på boksen:** vnext14 `7c508108` (vnext12 + `CYAN_LIGHT`-gating +
  frame/draw-tælling). Backup af den spillbare vnext12:
  `/root/egl_proxy_dcc0a68f.so.bak`.

## Status i ét blik (8. sep 2026 aften)

- Boks 1 kører Devuan armhf på eMMC med **genbygget 3.10.79-kernel**
  (`3.10.0 #1 SMP PREEMPT Sun Sep 6 22:12:06`), GPU = PowerVR DDK
  **1.5@3830101** (GLES 3.1), WebGL i Firefox virker.
- Boks 1 genstartet 8. sep aften → IP **192.168.0.171**; bring-up efter
  strøm: ur-sync, `insmod` + `gpu_up.sh`, bindapi-lap, `/dev/sw_sync` 0666.
- **Opdateret 8. sep 21:15 (cyan-scene):** shadow-probe (vnext5-9) viste at
  verdens-draws live skriver 0 også ved umiddelbar gentagelse; DEPTH-TEST er
  porten (var-depthoff rasterserer; loading skriver med tvungen depth-clear,
  gameplay gør endnu ikke — åbent). Præsentationssymptom (spilområde forsvinder
  ved play-start) gentaget 4×; canvas bliver i DOM (836x470) men compositoren
  taber laget → poki-baggrund ses; alpha-shim-test endnu inkonklusiv. Detaljer:
  `docs/log/2026-09-08-cyan-scene.md` (checkpoints 20:30-21:15).
- **OPDATERET 8. sep ~22:00 (cyan-scene): SPILLET ER SPILBART** — vnext12
  (md5 `dcc0a68f`) = alpha-shim (BiDi-preload, attrs alpha:true bekræftet) +
  depthmask-lap + depth-clear ÉN gang pr. frame med EKSPLICIT clearDepthf(1)
  (CYAN_DEPTHCLEAR=1). Dreng/tog/bygninger i korrekt perspektiv; brune flader
  væk. Tilbage: blå glitches + lav fps (instrumentering). Opskrift og beviser:
  `docs/log/2026-09-08-cyan-scene.md` (checkpoint 21:40-22:00).
- Cyan-scene-sporet (8. sep): drawBuffers **ude** (`[COLOR_ATTACHMENT0]`),
  instanced-stien og `glMapBufferRange` virker offline; VBO/EBO-snapshots +
  matricer viser store prog7-draws **100 % i frustum**; offline-replay af
  rigtige data rasteriserer (q38 7.218 px, q37 72.945 px). Live: alle
  attrib-divisor=0, clearDepthf=1, men verdens-draws skriver stadig ingen
  fragmenter (nonsky statisk; FS har ingen discard). **live-vs-offline-
  modsigelsen er det åbne spor** → næste: shadow-draw-probe i live-proxyen.
- Præsentationssymptom (2× målt 8. sep): ved "Press to play"-start forsvinder
  spilområdet fra siden — kør næste gang med poki-fix-udvidelsen/alpha-shim.
- Proxy på boksen: **vnext12 `dcc0a68f`** (alpha-shim via BiDi-preload +
  depthmask-lap + depth-clear 1×/frame med eksplicit `clearDepthf(1)`; kræver
  env `CYAN_DEPTHCLEAR=1`). Backups i `/root/egl_proxy_*.so.bak`.

## De mange elementer — hvad er hvad

| Lag/element | Hvad det er | Hvor | Status/kommando |
|---|---|---|---|
| **Kernel 3.10** | Genbygget vendor-kernel (PVR **fra**, TRACING, VT, MODULES) | `devuan/gpu/kernelbuild/` — byg: `build_kernel.sh`; kørende config: `out/test.config`; flashet billede: `out/clone3fix/ramfs-clone3fix.img` | Kører på boks 1 (se §Kernen) |
| **Android-system.img** | Vendor-/hybris-blobs (EGL/GLES/gralloc) mountes loop-ro som `/system` | myinit: `/usr/local/share/libhybris/system.img` → `/system` | Persistent pr. boot |
| **bindapi-lap** | Patcher Android-libEGL så ES-bind (`eglBindAPI`) virker; ES-config på ES2-sti | Script: `devuan/gpu/eglplatform_x11/patch_android_bindapi.sh`; patchet kopi `/root/egl_patch/libEGL.so` bind-mountet over `/system/lib/libEGL.so` | **Forsvinder ved genstart** → kør igen efter strøm (IP i scriptet skal sættes) |
| **GPU-stak (bring-up)** | logd + servicemanager + pvrsrvctl + PVR-KM-modul | `/root/pvrsrvkm_leddaz.ko`, `devuan/gpu/gpu_up.sh` | `insmod …; sh gpu_up.sh` efter hver boot |
| **sw_sync-fix** | `/dev/sw_sync` 0666 (gralloc-lock EINVAL-fix) | `devuan/myinit.sh` (retry-løkke) | Persistent; tjek med `ls -la /dev/sw_sync` |
| **EGL-proxy** | Vores `libEGL.so.1.0.0` i `/opt/hybris`: fix_egl_table + shader-hooks (frag_depth/vSupport) + draw/fbo/tex/uniform/postdraw/drawBuffers-log | Kilde: `devuan/gpu/eglplatform_x11/egl_proxy.c` → byg på boksen → `/opt/hybris/libEGL.so.1.0.0` | Log: `/tmp/cyan_draw_probe.log`, `/tmp/fragdepth_probe.log`, shader-kilder `/tmp/shaders/` |
| **libGLESv2** | SKAL være ORIGINAL (glesv2-proxyen brød WebGL1/ES1) | `/opt/hybris/libGLESv2.so.2.0.0` (md5 `ca71fb2c…` = backup) | Uændret |
| **Firefox-profil** | Prefs: software-layers, webrender fra, webgl tvunget, telemetri fra, remote-debugging | `/home/kristian/ffprof/` (på boksen) | Ingen ændring nødvendig pr. kørsel |
| **Firefox-shims** | `system_shim.so` + `egl_platform_shim.so` (LD_PRELOAD) | `/usr/local/lib/firefox-webgl/` | Sættes af start-scriptet |
| **Start-script** | Ren Firefox-start med BiDi-port 9222 + log-rydning | `devuan/gpu/eglplatform_x11/start_cyan_probe.sh` → `/root/start_cyan_probe.sh` | `nohup … &` |
| **Poki-fix-udvidelse** | Tvinger `alpha:true`/`premultipliedAlpha:true` + blokerer loseContext (fik canvas vist) | `devuan/gpu/eglplatform_x11/poki-fix-extension/`; xpi i profilen | Indlæses midlertidigt via about:debugging (eller BiDi-preloaden gør samme trick) |
| **BiDi-probe** | WebDriver BiDi-klient (port 9222): preload/reload/wait/dump/scene/domcheck/glstate | `devuan/gpu/eglplatform_x11/bidi_ctxloss.py` → `/root/bidi_cyan.py` | Kør på boksen; **fælde:** BiDi-session → poki `bot=1` → spillet fryser ved 0% ved reload |
| **Scene-replay-probe** | Poki-FRIT test: kompilerer spillets fangede shaders og tegner syntetisk geometri på 1.5 | `devuan/gpu/eglplatform_x11/scene_replay_probe.c` + `/root/p7.vs`+`p7.fs` (boks) | Resultat: FS + VS+FS rasteriserer korrekt |
| **Skærm/readback** | fb0 = 1920×1080 RGB565 (line 3840); fbdump-værktøj skal scp'es (mangler på boksen) | `/dev/fb0` | `readPixels`/`toDataURL` uden for frame er ubrugelig (`preserveDrawingBuffer=false`) |

## Kernen — hvad er der præcist skrevet?

1. **26. aug-byg (test-trace2, tidligere kørende):** compat-tabellen udvidet
   384 → 404 poster; syscall **403** (`clock_gettime64`, brugt af bionic 6.0)
   → `sys_clock_gettime`.
2. **6. sep-byg (KØRER NU, flashet på boks 1):** compat-tabellen udvidet til
   **450 poster**: 403-fixet bevaret, og **404..449 → `sys_ni_syscall`**
   (ren ENOSYS). Vigtigst: **435 = `clone3`** — Firefox' `glean.upload`-tråd
   kaldte den, og 3.10-kernen dræbte processen (SIGILL) i stedet for ENOSYS →
   Firefox crashede 1½-3 min efter start. Nu får kalderen ENOSYS og falder
   tilbage på `clone`.
3. Config er **identisk** med test-trace2 (`out/test.config`, diff-verificeret)
   → `pvrsrvkm_leddaz.ko` forbliver kompatibel (vermagic uændret).
4. Filer: `devuan/gpu/kernelbuild/build_kernel.sh` (patchen indbygget),
   `out/clone3fix/ramfs-clone3fix.img` (30.638.080 B, id `d91b6871…`).
   Rollback: `out/test-trace2/ramfs-test-trace2-id.img` (forrige) og
   `extracted/Image/ramfs.img` (original). Flash kun boot-partitionen:
   `upgrade_tool DI -b <img>` i loader-tilstand.

## EGL-proxy-versioner (md5 → funktion)

| md5 | Funktion | Bemærkning |
|---|---|---|
| `91651801…` | v0-baseline (shader-hooks) | Håndover-kendt-god |
| `398e71dd…` | v2: + draw/fbo/tex/uniform-instrumentering | **Stabil**; bruges til almindelige kørsler |
| `9a8127a4…` | + efter-draw-readback (postdraw) | Viste at store draws skriver 0 pixels |
| `90006e6b…` | + vertex-buffer-læsning | `glGetBufferSubData` findes IKKE på stakken (NULL) |
| `715716d0…` | + `glDrawBuffers`-log + `GL_DRAW_BUFFER0..3` | drawBuffers-sporet (8. sep: attachment-forklaringen ude) |
| `63fe4c6c…` | + VBO/EBO-snapshot via `glMapBufferRange` + pre/post-grid-diff | Stabil (kørsel 4) |
| `e4e4ca74…` | vnext2: + fuld-frame pre/post-diff | **Crashede 2/2 ved første store draw — brug ikke** |
| `746522ec…` | vnext3: vnext minus fuld-frame | Stabil (kørsel 7) |
| `631f9a40…` | vnext4: + nonsky-tælling + `clearDepthf`-log + divisor-log | Stabil (kørsler 8-9), men afløst |
| vnext5-11 | shadow-draw-probe, force-clear m.m. (8. sep) | Diagnose — se `docs/log/2026-09-08-cyan-scene.md` |
| `dcc0a68f…` | vnext12: + alpha-shim, depthmask-lap, depth-clear 1×/frame | **NUVÆRENDE på boksen** — spillet er spilbart |
| `7c508108…` | vnext14: vnext12 + `CYAN_LIGHT`-gating (slår al måle- instrumentering fra) + GL-tick/draw-tæller | Bruges til fps-måling, 17. sep 2026 |

## Nøglekommandoer (boksen efter strømcyklus)

```bash
bash devuan/find_box.sh                     # IP skifter pr. boot
# ssh: ssh -i ~/.ssh/geekbox_key -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@<ip>
date -s @$(date +%s)                         # boksen har ingen RTC
insmod /root/pvrsrvkm_leddaz.ko && sh /root/gpu_up.sh
ls -la /dev/sw_sync                          # skal være 0666
# bindapi (fra laptoppen, IP rettes):
sed "s/BOX=root@192.168.0.188/BOX=root@<ip>/" \
  devuan/gpu/eglplatform_x11/patch_android_bindapi.sh > /tmp/pb.sh && bash /tmp/pb.sh
# Firefox + måling:
nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &
# proxy-genbyg på boksen (repoets egl_proxy.c ER vnext12 — scp den over som /root/egl_proxy_vnext12.c):
gcc -shared -fPIC -Wl,-soname,libEGL.so.1 -o /tmp/libEGL_vnext12.so /root/egl_proxy_vnext12.c \
  -L/opt/hybris -Wl,--no-as-needed -l:libEGL_r.so -ldl && cp /tmp/libEGL_vnext12.so /opt/hybris/libEGL.so.1.0.0
# poki-frit instanced/map-test + replay af fangede buffers:
LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 \
  /root/scene_instanced_probe /root/p7.vs /root/p7.fs
LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 \
  /root/scene_real_replay /root/p7.vs /root/p7.fs <pos.a1> <nrm.a0> <uv.a2> <idx> <mats> <count>
```

## Dokumentindex (README/session/handover)

| Fil | Indhold |
|---|---|
| `README.md` | Projektets formål + flash-veje + upgrade_tool-brug |
| `TODO.md` | Opgaveliste/spor (seneste pointer øverst) |
| `docs/` | Al dokumentation — indgang i `docs/README.md` |
| `docs/grafik/hvorfor.md`, `docs/grafik/firefox-webgl.md`, `docs/boksen/skaerm.md` m.fl. | Ældre del-emner (grafikstak, browser-veje, skærm) |
| `devuan/gpu/README.md` | GPU-arbejdsområde (værktøjer, historik) |
| `docs/log/2026-08-26-ddk*.md` | DDK 1.5-installation, baseline-flashtest, kernel-rebuild |
| `docs/log/2026-08-26-gralloc-lock-spor.md` | Beslutnings-/målelog: præsentation virker, spillet spilbart, cyan-scene åben |
| `docs/log/2026-08-27-cyan-scene-handover.md` | **Opgaverammen:** cyan-scene + fuld virkende konfiguration + fælder |
| `docs/log/2026-09-06-cyan-clone3.md` | **Seneste session:** kernel clone3-fix, GL-målinger, replay-probe, load-stall-fælder |
| `docs/log/2026-09-08-cyan-scene.md` | **Seneste session (8. sep):** divisor ude, live-vs-offline, præsentationssymptom, nye probes/fælder |
| `devuan/gpu/eglplatform_x11/*.c/.py/.sh` | Kode: proxy, probes, scripts (se §De mange elementer) |

## Åbne spor / næste skridt

- **Cyan-scene (SPILBAR, 8. sep aften):** rest = blå glitches (ikke undersøgt),
  selvstart af alpha-shim + depth-clear (BiDi-preload + env i dag), og
  driver-rodårsagen til clearDepthf (isolerede prober viser at driveren er
  korrekt i alle enkelt-kontekst-tests; live drifter queryen til 0/skrald i
  ~2,5 % af målene → næste skridt er en fler-trådet probe).
- **fps-sporet (ÅBENT, 19. sep sen aften):** målt i spillet er loftet
  **browserens compositing/præsentation** — en triviel CSS-side uden WebGL
  giver kun 1,1 skærm-opdateringer/s og 5,6 rAF/s i samme opsætning, mens
  spillet giver 2,4-2,6 fps/2,4-2,6 opdateringer/s. Draw-kald er kun ~10 ms af
  de ~400 ms pr. frame (~250-280 kald/frame; 0,099 ms/kald i driveren, som
  `drawbench_probe` måler uden om proxyen, + 0,004 ms/kald for proxyens egne
  kald). Rettelse: de gamle "~2.000-2.700 kald/frame" var en JS-tællerfejl
  (~8x). Næste forsøg (forslag, se TODO-prompten): compositing-vejen (mindre
  vindue/canvas, `gfx.webrender.software`, `layers.acceleration.disabled`) og
  720p gjort rigtigt (HDMI-mode + `fbset`/genstart). `CYAN_LIGHT` frøser
  fortsat præsentationen. Blå glitches er lagt til side indtil videre.
- NTP/chrony (myinit-synk virker som plaster).
- Næste kernel-byg (planlagt): `CONFIG_ANDROID_PARANOID_NETWORK` fra +
  bcmdhd (WiFi) — samme byggevej som dagens.
- Spor B: mainline-kernel/nyere Linux (headless; HDMI/GPU mangler).
