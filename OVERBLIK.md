# GeekBox (RK3368) + Linux + Subway Surfers WebGL — overblik

> Oprettet 6. sep 2026 som samlet indgang til projektets mange elementer.
> Læs først: [README.md](README.md) (formål/flash), seneste session
> `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md` (seneste),
> `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md` (opgavebaggrund) og
> `devuan/gpu/GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md` (beslutningslog).

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
  `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md` (checkpoints 20:30-21:15).
- **OPDATERET 8. sep ~22:00 (cyan-scene): SPILLET ER SPILBART 🎉** — vnext12
  (md5 `dcc0a68f`) = alpha-shim (BiDi-preload, attrs alpha:true bekræftet) +
  depthmask-lap + depth-clear ÉN gang pr. frame med EKSPLICIT clearDepthf(1)
  (CYAN_DEPTHCLEAR=1). Dreng/tog/bygninger i korrekt perspektiv; brune flader
  væk. Tilbage: blå glitches + lav fps (instrumentering). Opskrift og beviser:
  `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md` (checkpoint 21:40-22:00).
- Cyan-scene-sporet (8. sep): drawBuffers **ude** (`[COLOR_ATTACHMENT0]`),
  instanced-stien og `glMapBufferRange` virker offline; VBO/EBO-snapshots +
  matricer viser store prog7-draws **100 % i frustum**; offline-replay af
  rigtige data rasteriserer (q38 7.218 px, q37 72.945 px). Live: alle
  attrib-divisor=0, clearDepthf=1, men verdens-draws skriver stadig ingen
  fragmenter (nonsky statisk; FS har ingen discard). **live-vs-offline-
  modsigelsen er det åbne spor** → næste: shadow-draw-probe i live-proxyen.
- Præsentationssymptom (2× målt 8. sep): ved "Press to play"-start forsvinder
  spilområdet fra siden — kør næste gang med poki-fix-udvidelsen/alpha-shim.
- Proxy på boksen: **vnext4 `631f9a40`** (grid + nonsky-tælling +
  clearDepthf-log + VBO/EBU-snapshots; stabil). Backups i `/root/egl_proxy_*.so.bak`.

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
| `631f9a40…` | vnext4: + nonsky-tælling + `clearDepthf`-log + divisor-log | **NUVÆRENDE på boksen**; stabil (kørsler 8-9) |

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
# proxy-genbyg på boksen (efter ændring af egl_proxy.c → scp til /root/egl_proxy_vnext4.c):
gcc -shared -fPIC -Wl,-soname,libEGL.so.1 -o /tmp/libEGL_vnext4.so /root/egl_proxy_vnext4.c \
  -L/opt/hybris -Wl,--no-as-needed -l:libEGL_r.so -ldl && cp /tmp/libEGL_vnext4.so /opt/hybris/libEGL.so.1.0.0
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
| `DOKUMENTATION.md`, `HAANDBOG.md` | Samlet viden + fælder (opdateres løbende) |
| `GRAFIK-FORKLARET.md`, `BROWSER-VEJE.md`, `DEBUG-SORT-SKAERM.md` m.fl. | Ældre del-emner (grafikstak, browser-veje, skærm) |
| `devuan/gpu/README.md` | GPU-arbejdsområde (værktøjer, historik) |
| `devuan/gpu/DDK15-*-HANDOVER/NOTAT-2026-08-26.md` | DDK 1.5-installation, baseline-flashtest, kernel-rebuild |
| `devuan/gpu/GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md` | Beslutnings-/målelog: præsentation virker, spillet spilbart, cyan-scene åben |
| `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md` | **Opgaverammen:** cyan-scene + fuld virkende konfiguration + fælder |
| `devuan/gpu/CYAN-CLONE3-SESSION-NOTAT-2026-09-06.md` | **Seneste session:** kernel clone3-fix, GL-målinger, replay-probe, load-stall-fælder |
| `devuan/gpu/CYAN-SCENE-SESSION-NOTAT-2026-09-08.md` | **Seneste session (8. sep):** divisor ude, live-vs-offline, præsentationssymptom, nye probes/fælder |
| `devuan/gpu/eglplatform_x11/*.c/.py/.sh` | Kode: proxy, probes, scripts (se §De mange elementer) |

## Åbne spor / næste skridt

- **Cyan-scene (aktivt):** fang `glDrawBuffers`/buffer-data på en vellykket
  load+play → afgør attachment/frustum. Husk: kølepause ved load-stall,
  ingen BiDi-reload under load.
- NTP/chrony (myinit-synk virker som plaster).
- Næste kernel-byg (planlagt): `CONFIG_ANDROID_PARANOID_NETWORK` fra +
  bcmdhd (WiFi) — samme byggevej som dagens.
- Spor B: mainline-kernel/nyere Linux (headless; HDMI/GPU mangler).
