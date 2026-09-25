# FPS-analysen 25. sep 2026 — hvor sidder loftet, og hvad kan vi gøre?

> Bredere analyse af fps-loftet på den nuværende stak (Devuan armhf + hybris
> EGL/GLES + X fbdev + Firefox ESR 140 med software-layere på RK3368/GeekBox).
> Bygger på `docs/log/2026-09-25-cyan-fps.md` (dagens målinger),
> `docs/log/2026-09-20-cyan-fps.md` (areal-testen), `docs/faeller.md` fælde
> 44-51, `docs/grafik/driver-portering.md` og
> `docs/log/2026-08-26-ddk-handover.md` (4.4-kernel-sporet).

## 1. Konklusion (kort)

**Loftet ligger inde i Firefox' kompositor/present-sti — ikke i X, ikke i
framebufferen og ikke i CPU'en.** Alle tre nederste lag er målt i dag og er
hurtige:

| Lag | Måling 25. sep 2026 | Vurdering |
|---|---|---|
| CPU -> framebuffer (`memcpy` til `/dev/fb0`) | **3,1 ms** for et helt 1080p-frame (1.285 MB/s) | ~325 fps. Ikke flaskehalsen |
| `memset` af hele fb | 1,9 ms (2.050 MB/s) | Ikke flaskehalsen |
| Delvise fb-rektangler (640x360, 1280x720, 1920x1054) | 0,2 / 0,6 / 1,4 ms | Skalerer fint med arealet |
| X-serverens egen presentvej (`XPutImage`, XSync pr. frame) | **11,4 ms** for 1920x1054 (87,8 fps), 2,15 ms for 640x360 | ~40x hurtigere end Firefox |
| CPU-frekvens (DVFS) | 312 MHz i tomgang -> **1200/1296 MHz under last** | Skalerer korrekt |
| Firefox' faktiske leverance | **2,4 skærm-opdateringer/s** ved 1920x1054 | ~40x langsommere end X |

Firefox' egen log siger det direkte:

```
[Child ...: Main Thread]: D/nsRefreshDriver [e441f6d0]
  Over max pending transaction limit when trying to paint, skipping
```

Dvs. indholdsprocessen vil gerne male, men har for mange uafklarede
transaktioner hos kompositoren — **kompositoren er den, der ikke kan foelge
med**, og den lægger backpressure på hele browseren. Det er de ~400 ms/frame.

### Hypoteser der nu er afvist (målt, ikke gættet)

| Hypotese | Hvordan afvist |
|---|---|
| "CPU-skrivningen til framebufferen er langsom (uncached/WC)" | `fb_bench`: 1.285 MB/s ind i fb0, 3,1 ms/frame |
| "X's fbdev-vej er langsom (ingen EXA/KMS)" | `xput_bench`: 11,4 ms for et fuldt 1920x1054-vindue |
| "Compositing manager oveni (xcompmgr/picom) koster et ekstra gennemløb" | Ingen CM kører; `_NET_WM_CM_S0` har ingen ejer |
| "Firefox tegner i ARGB (depth 32) og tvinger X til per-pixel-konvertering" | Firefox' hovedvindue er Depth 16 / Visual 0x21 = samme format som roden |
| "CPU'en står i et lavt DVFS-trin under målingen" | 8 kerner, `interactive`, ramper til 1200/1296 MHz under last |
| "`fb_fps` med for lav poll gør tallene forkerte" | Målt med POLL=20 (fælde 49) |

## 2. Hvor mange pixels koster hvad (målt 25. sep)

| Stak | Vindue | Pixels | Rate | Tid/frame | Effektiv pris |
|---|---|---|---|---|---|
| X-klient, `XPutImage` (dybeste lag) | 1920x1054 | 2,02 Mpx | 87,8/s | 11,4 ms | **0,006 µs/px** |
| X-klient, `XPutImage` | 640x360 | 0,23 Mpx | 466/s | 2,15 ms | 0,009 µs/px |
| Kontrolside i Firefox (fuld gentegning) | 1920x1054 | 2,02 Mpx | 2,4/s | 417 ms | **0,20 µs/px** |
| Kontrolside i Firefox | 1280x720 | 0,92 Mpx | 6,0/s | 167 ms | 0,18 µs/px |
| Kontrolside i Firefox (20. sep) | 1000x600 | 0,60 Mpx | 10,3/s | 98 ms | 0,16 µs/px |
| Spillet (Subway Surfers) | 640x360 | 0,23 Mpx | 4,2-4,8/s | 210-240 ms | ~1,0 µs/px |
| Spillet | 1280x720 | 0,92 Mpx | 1,8-1,9/s | 526-555 ms | ~0,6 µs/px |
| Spillet (19.-20. sep) | 1920x1054 | 2,02 Mpx | 2,4-2,6/s | 385-415 ms | ~0,2 µs/px |

To ting står ud:

1. **Firefox' egen pris er ~30x X'ens** for det samme antal pixels. Det er
   *browserens* software-kompositing, der koster 0,2 µs/px — ikke skærmen.
2. **Spillet er ikke monotont i areal:** 0,23 Mpx -> 4,5/s, 0,39 Mpx (canvas
   836x470 i et 1280x720-vindue) -> 1,85/s, 0,60 Mpx (canvas 1031x580 i
   1920x1054-vindue) -> 2,5/s. Hverken vindues- eller canvas-areal forklarer
   rækkefølgen. Det er et *åbent* spørgsmål (se §6).

## 3. Den nuværende Firefox-opsætning (læst af profilen)

| Pref | Værdi | Betydning |
|---|---|---|
| `gfx.webrender.enabled` | `false` | WebRender slået fra |
| `gfx.webrender.force-disabled` | `true` | WebRender kan ikke slås til af siden |
| `gfx.webrender.software` | `false` | SWGL heller ikke i brug |
| `layers.acceleration.disabled` | `true` | Ingen GPU-accelererede lag |
| `layers.gpu-process.enabled` | `true` (user.js) | GPU-processen er tilladt |
| `gfx.x11-egl.force-enabled` | `true` | X11-EGL-stien tvinges |
| `MOZ_X11_EGL=1`, `EGL_PLATFORM=x11`, `LD_LIBRARY_PATH=/opt/hybris` | sat | Firefox bruger vores hybris-EGL |

Vi kører altså den **ældste** kompositor-vej i Firefox (Basic/software) med
WebRender eksplicit slået fra — den vej, alle anbefaler at forlade. Det er den
mest sandsynlige enkeltårsag til de 0,2 µs/px.

## 4. 4.4-kernel-sporet (Spor B) — status og revurdering

**Hvad der blev fundet 26. aug 2026** (brugerens spørgsmål dengang):

- Firefly `firefly-linux-kernel-4.4.55` har RK3368-understøttelse **og** et
  PVR-DDK-1.8-kbuild-overlay (`Rogue_DDK_Android_REL_1.8.RTM@4610191`).
- Der findes **ingen GeekBox-DTS til 4.4** (radtw har 3.10 og 3.18;
  mainline-`rk3368-geekbox.dts` er headless: seriel, USB, GMAC, eMMC, IR,
  TSADC — ingen display/GPU).
- Rockchip skiftede display til **DRM/KMS fra 4.4**; hele vores rk_fb-fbdev-,
  myinit- og eglplatform_x11-integration samt Android-system.img
  (Android 7.1-æra, formentlig 64-bit userspace mod vores bevidste 32-bit
  hybris) skal skifte med. Boot-kæden (U-Boot/ATF) skal også med.
- Konklusionen dengang: "teknisk muligt, men et porteprojekt (uger), ikke en
  opgradering" — og `docs/grafik/driver-portering.md` kalder 3.10 -> 4.4 for
  "brudt / år, reelt uoverkommeligt" for driverne.

**Revurdering efter dagens målinger:** 4.4'ets store gevinst for *præsentation*
ville være KMS/page-flip i stedet for CPU-kopier til fbdev. Men dagens målinger
viser, at **fbdev-vejen kun koster 11,4 ms/frame** (XPutImage) og 3,1 ms
(direkte memcpy til fb). Der er altså ikke 400 ms at hente i det lag. KMS ville
være rart, men løser ikke det, vi måler.

Til gengæld er **DDK 1.8** (den nyere PowerVR-driver, som kun findes i
4.4-æraen) stadig den eneste kendte vej til en *bedre GL-driver* — og hvis
WebRender/GPU-kompositing skal være realistisk, er en bedre GL-driver
sandsynligvis en forudsætning. Det er den reelle begrundelse for Spor B nu;
ikke fps-loftet i sig selv.

## 5. Nye spor, prioriteret

Rangordnet efter (forventet gevinst) / (indsats + risiko). Alle kan måles med
`cyan_ab_run.sh` + `fb_fps` (POLL=20) + rAF-linjen, som vi allerede bruger.

### S1. Skift Firefox' kompositor-vej (størst potentiale, billigst)
Vi kører i dag den langsomste vej (Basic/software). Tre varianter at måle i
*kopier* af profilen, så originalen kan genskabes:

1. `gfx.webrender.software=true` — WebRender med SWGL (moderne software-
   rasterizer, skrevet til at være hurtig på svag CPU).
2. `gfx.webrender.enabled=true` + `force-disabled=false` +
   `layers.acceleration.disabled=false` — WebRender på GPU'en (hybris-EGL).
   Historik: gav sort skærm 17. sep og DeviceReset 25.-26. aug, men stakken er
   siden ændret (1.5-KM som `.ko`, clone3-fix, buffer-fix) — og 24. aug-noten
   viser at WebRender-hardware *nåede* `present #2` med `MOZ_GL_SPEW` slået fra.
3. `layers.gpu-process.enabled=false` + `layers.omtp.enabled=false` — færre
   procesgrænser/kopier i den software-vej vi har.

Succeskriterium: rAF og skærm over 6/s ved 1080p er en klar sejr; 0 opdateringer
eller sorte frames = rul tilbage (fælde 46/47-hukommelse: livlinen er
`service nodm restart`, og profilen har backup).

### S2. Profilér hvor de 400 ms ligger (gør S1 målrettet)
`start_cyan_probe.sh` sender nu `MOZ_LOG`, `MOZ_LOG_FILE`,
`MOZ_PROFILER_STARTUP` og `MOZ_PROFILER_SHUTDOWN` videre (den bruger `env -i`).
Med `MOZ_LOG=Compositor:5,LayerManager:5,Paint:5,nsRefreshDriver:5` får vi
kompositorens egne tidstagninger, og med `MOZ_PROFILER_STARTUP=1` +
`MOZ_PROFILER_SHUTDOWN=/tmp/prof.json` en tråd-profil af hele kørslen.
Det svarer på *hvilken* del af kompositoren der bruger tiden (rasterisering,
skalering, kopi mellem processer, X-present).

### S3. Skru ned for pixels browseren skal komposere (virker uden nye drivere)
Dagens tal viser at spillet ved 640x360 renderer ~4,5 fps. For at få det i et
*større* vindue kan vi lade spillet rendere småt og lade browseren skalere op:
en BiDi-preload der overskriver `HTMLCanvasElement.prototype` sine
`width`/`height`-sættere (fx halverer dem) mens CSS-størrelsen bevares.
Så renderer Unity 640x360 og browseren skalerer til vinduet.
- Fordel: ingen display-ændringer, ingen kernel/driver-arbejde.
- Risiko: Unity kan regne musekoordinater ud fra canvas-størrelsen — skal
  efterprøves ved at spille.
- Bonus-test i samme spor: `image-rendering: pixelated` på canvas (billigere
  opskalering end bilineær filtrering).

### S4. Systemhåndtag (små, billige, usikre)
- `scaling_governor=performance` (1200 MHz fast) under spil — fjerner
  ramp-latensen fra 312 MHz. Koster strøm/varme.
- `ddrfreqd`/DDR-frekvens: boksens DDR-governor kan holdes på højeste trin.
- `vm.swappiness=10` + tjek for swap-trafik under kørsel (`vmstat 1`).

### S5. Præsentationsstien udenom browseren (stor, men dyr)
Vi har tidligere målt at en *egen* GLES-præsentationsvej (`eglplatform_x11`,
M2/M3) kan levere ~10 fps ved fuld skærm for en simpel scene. At flytte selve
spillet ud af browseren er urealistisk, men princippet kan bruges til at
*bevise* hvor loftet ligger: en minimal GLES-klient der tegner 2 Mpx i 60 fps
gennem hybris-stakken viser om noget i EGL/gralloc (ikke Firefox) sætter en
grænse. (Billig at bygge: vi har proberne.)

### S6. Andre browsere / andre veje (lav prioritet)
Chromium-hybris (TARGET_HYBRIS) blev undersøgt 24. aug: Chromium 47 har både
GLX- og EGL/GLES2-vej. En moderne browser kan ikke bygges på denne stak, og
den gamle kan næppe køre spillet. Beholdes som nødløsning, ikke som spor.

## 6. Åbne spørgsmål (skal måles, ikke gættes)

1. **Hvorfor 0,2 µs/px inde i Firefox?** Kompositoren gør noget per pixel, som
   platformen under den kan gøre 30x hurtigere. S2 (profilering) er vejen.
2. **Hvorfor er spillets pris ikke monoton i areal?** 0,39 Mpx canvas (1,85/s)
   er langsommere end både 0,23 Mpx (4,5/s) og 0,60 Mpx (2,5/s). Mulige
   forklaringer: forskellig skalering vinduet/canvas imellem, forskelligt
   indhold pr. frame (draws/frame varierer 90-266), eller forskellig
   layout-vej i poki ved små vinduer. Kræver måling af *ødelagt areal pr.
   frame* (fb_fps' blok-histogram) og canvas-geometri (domcheck, se §7).
3. **Hvorfor crashede spillets indholdsproces 25. sep i 1080p?** (`PC=0x2`,
   `Comm: Web Content`, ingen PVR-MMU-fault). Én hændelse indtil videre;
   følges op hvis den gentages — den kan være den samme sti som de gamle
   GPU-resets, eller hukommelsesrelateret.

## 7. Værktøjer og bænke (25. sep 2026)

Nye i repoet (og kopieret til boksen):

| Værktøj | Hvad det måler | Kør |
|---|---|---|
| `fb_bench.c` | Rå skrivehastighed til `/dev/fb0` (helt frame, rektangler, memset) | `/root/fb_bench /dev/fb0` |
| `xput_bench.c` | X-serverens egen presentvej (`XPutImage`, XSHM hvis header findes) | `DISPLAY=:0 /root/xput_bench 1920 1054 16 20` |
| `cyan_ab_run.sh` | A/B-kørsel: skærm (`fb_fps`, POLL), rAF, GL-tælling, vinduesgeometri | se `docs/log/2026-09-25-cyan-fps.md` |
| `start_cyan_probe.sh` | Sender nu `MOZ_LOG`/`MOZ_PROFILER_*` videre gennem `env -i` | `MOZ_LOG=Paint:5 ... bash /root/cyan_ab_run.sh ...` |

**Kendt fejl der skal rettes:** `cyan_ab_run.sh` kalder `bidi_cyan.py domcheck`
til sidst, men preload-klienten lukker ikke sin BiDi-session, så svaret bliver
"Maximum number of active sessions". Indtil det er rettet, læses
canvas-størrelsen af proxyens log (`texImage2D`/`fboTex`, den største tekstur).

## 8. Anbefalet rækkefølge

1. **S1.2 (WebRender/GPU) og S1.1 (SWGL) som A/B mod i dag** — størst
   potentiale, og svaret ligger få målekørsler væk. Med profilkopi + fallback.
2. **S2. profilering** hvis S1 ikke giver et gennembrud — så vi ikke gætter.
3. **S3. canvas-nedskalering** — den eneste vej der er målt til at give ~2x
   *uden* at røre driver eller display.
4. **S4. systemhåndtag** som billig topping.
5. **Spor B (4.4 + DDK 1.8)** kun hvis S1-S3 viser at vi har brug for en bedre
   GL-driver — og med åbne øjne om at det er uger og kræver seriel konsol.
