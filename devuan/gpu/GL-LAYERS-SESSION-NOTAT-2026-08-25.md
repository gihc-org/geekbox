# GL-layers-forsøg — session-notat 25. aug 2026 (aften/nat)

## Aftaler og beslutninger

- [udført] Lokal stress-side bygget + kørt — fejlen reproduceres uden Unity/reklamer/netværk; present-stien er synderen (25. aug nat, 21:25).
- [udført] stall_capture.sh rettet (GPU-lookup + present-nummer + rate-check), rootdiff 16/32-bpp, capture_stress.sh bygget (21:30).
- [udført] xrefresh installeret via x11-xserver-utils (21:38).
- [udført] Boks genstartet efter D-state-kile (sysrq-b), GPU-init + patches genkørt, værktøjer genopbygget (21:40).
- [aftalt] Næste: uBlock Origin som kontrol → gdb på GPU-processen ved stall → xrefresh-test → andre WebGL-sider (Shadertoy, aquarium/three.js, Basemark sidst).
- [afventer] Remote debugging-port 9222 aldrig observeret lyttende — uafklaret, ikke kritisk.
- [udført] uBlock Origin 1.74.0 installeret i profilen (22:05) — kontrol, ikke årsag.
- [udført] Run C (ingen læsninger): GPU-proces DeviceReset WR_POST_UPDATE ~1 min inde → skærm sort → desktop tilbage, vinduet animerer i X (87k px/3 s) men når ikke skærmen; gdb viser Renderer-tråd i present()-konverteringsloop (presents KØRER); X sund, vindue+ramme mapped; x32probe4 segfault i denne tilstand (22:11).
- [målt] system-shim HDMI-dans ved start (chvt 7, HDMI 0, HDMI 1, chvt 11, chvt 7) — sort-øjeblik ved launch.
- [målt] fb-driver-read-wedge er et SEPARAT kernel-problem: XGetImage(root)/dd(fb0) kiler fb-driverens read() i D-state under present-belastning (run B) — ikke X-serveren.
- [udført] Run C konklusion (bruger-observation + pixel-analyse 22:14): RESTEN af skærmen har fine farver — KUN Firefox-vinduets indhold er sort; openbox-titellinjen (tekst) er læsbar. Vindue-dump er ~100 % sort (mean 0-2, 0 % lyse pixels); de 78k px/4 s ændringer er støj. rAF/FPS kører videre, presents fortsætter (#800+), men indholdet er SORT. Ingen CONTEXT_LOST logget, ingen GL-fejl i loggen.
- [revideret] KONKLUSION: frysen er IKKE en present-/kompositeringsfejl — det er en RENDER-fejl efter GPU-proces-genstart: WebRender tegner sorte frames ind i EGL-overfladen. X-serveren kompositerer fint (desktop + openbox-ramme + øvrige vinduer vises). Den tidligere "present-stien er synderen"-konklusion er hermed kvalificeret/ændret.
- [målt] Run D (gfx:5, 22:17–22:21): DeviceReset allerede ved present #2–3 (<10 s inde) — reset-tidspunktet varierer altså (run C: ~#50; run D: ~#2). `MOZ_LOG=gfx:5` nåede GPU-processen (verificeret i environ) men gav INGEN ekstra log-linjer — den vej er blind i denne build.
- [aftalt] Ny test FØR videre Firefox-kørsler (22:21): **standalone GLES-loop gennem den samme eglplatform_x11-sti** (`test_client_x11` på boksen; evt. ny probe med frame-tæller + eglSwapBuffers-fejlkode) i 3+ min. Virker den uendeligt → fejlen er Firefox/WebRender-samspil; fejler/sortner den efter N frames → vendor-stakken (hybris/PVR/gralloc) er roden.
- [udført] Standalone GLES-loop (22:22–22:27): **300 swaps uden fejl (2,2 fps, 137 s), present #1→#300, X OK** — vendor-stakken (hybris/EGL/gralloc → eglplatform_x11 → XPutImage) overlever langt ud over Firefox' reset-punkt (#2–#100). **Fejlen er Firefox/WebRender-samspillet, IKKE vendor-stakken.**
- [målt] test_client artefakt: overfladen blev `fmt=4` (RGB_565) mens shim'ens put_image-konvertering er hårdkodet RGBA8888 (4 B/px) → 2×2-tiling + pixeleret nederste halvdel (bruger-observation + kvadrant-lysstyrke TL=TR=160, BL=82, BR=79). Testklient-specifikt; Firefox bruger fmt=1 (RGBA_8888). Værktøj skal rettes (respekter b->format) før test_client bruges til pixel-tjek.
- [målt] **Present-loft ~2M px/s gælder STANDALONE-klienten** (640×360 → ~9 fps, 1280×720 → 2,2 fps, 1280×948 → ~1,4 fps), men **Firefox' kompositor er kadence-bundet, ikke transfer-bundet**: scale=0,5 (640×407, run F) gav STADIG ~1,4 fps — Firefox' GL-layers-kompositor kører fast ~1,4 Hz uanset canvas-størrelse (vsync-kadence-problem på denne stak). rAF følger kompositor-takten.
- [udført] Run E (gfx:5+WebRender:5, 22:28–22:38): **10 min / present #800+ / 0 DeviceReset** — reset'et er FLAKY (run C ~#50, run D ~#2, run E ingen); MOZ_LOG gav stadig intet ekstra.
- [udført] Run F (scale=0,5, 22:38–22:47): **9 min / present #800+ / 0 DeviceReset** — hurtigere presents (~4× mindre canvas) fjerner IKKE reset'et (2 af 4 kørsler på denne boot reset: C+D; E+F overlevede). Reset'et er flaky, ikke canvas-størrelses-afhængigt.
- [målt] Firefox-kompositor-kadence ~1,4 Hz er uafhængig af canvas-størrelse (scale=1 og 0,5 giver samme FPS) — en separat vsync-kadence-problematik, ikke transfer-loftet (som kun gælder standalone-klienten).

## Buffer-race-hypotesen (rodårsag til GPU-proces-reset) — AFTALT 22:55

**Hypotese:** en buffer-race i shim'ens resize-sti.

Firefox opretter kompositorvinduet som 1×1 og resizer det bagefter
(loggen: `2 buffer(e) allokeret (1x1)` → `ændret størrelse -> 1280x948` →
`2 buffer(e) allokeret (1280x948)`). I `eglplatform_x11.cpp` betyder det:
2 stk. 1×1-gralloc-buffere allokeres, så kommer resize → `destroyBuffers()`
kaldes **uden at tjekke om en buffer stadig er i brug** (`busy=1`, GPU'en
renderer i den) → use-after-free → sporadisk GL-fejl → Firefox detekterer det
ved `WR_POST_UPDATE` → GPU-proces-reset. Flakinessen er race-timing.

Det forklarer også hvorfor standalone-klienten aldrig reseter: dens log viser
`vindue pakket ind (1280x720)` — direkte i slutstørrelse, ingen 1×1-dans,
ingen resize-race. Firefox er den eneste der laver 1×1→resize, og det er
præcis den der reseter. Samme historie passer med run D (reset ved present
#2-3, lige efter resize-dansen) og at resets er flaky.

**Plan (små skridt):**
1. **Fiks buffer-lifecycle i shim'en (vigtigst):** slet aldrig en `busy`
   buffer — markér `retired`, og destruér kun når ingen buffer er i brug
   (frigør ved queue/cancel). Genbyg `eglplatform_x11.so`, kør stress-siden
   4-6 gange, mål om reset-raten (~50 %) falder til ~0.
2. **Log swap/queue-fejl:** returværdi-logning i queueBuffer/present-stien,
   så en evt. fejl bekræftes i loggen (ikke kun som Firefox' WR_POST_UPDATE).
3. **Verificér hvilken kompositor der faktisk kører:** GPU-processens tråde
   hedder "WRRenderer/WRSceneBuilder" selvom `gfx.webrender.enabled=false`
   står — tjek om WebRender reelt er aktivt (fx `gfx.webrender.force-disabled`).
4. **Reproduktionstest:** tving window-resizes under kørslen (xdotool/wmctrl)
   og se om reset-raten stiger — bekræfter resize-racen som udløser.
5. **Kadence-problemet (~1,4 Hz)** er en separat sag, tages bagefter.

## Opdatering 23:10 — WebRender kører reelt (plan-skridt 3 besvaret)

**`gfx.webrender.enabled=false` er IKKE effektivt:** GPU-processen har 25
tråde, alle WR* (WrGlyphRasterizer, WRWorkerLP#0-7, WRWorker#0-3, ...).
Firefox 128 ESR kører altså WebRender-på-OpenGL i denne "GL-layers"-tilstand.
Det forklarer `WR_POST_UPDATE`-reset'et (WebRender-detektion) og passer med
alt det målte.

**Buffer-race-fixet (skridt 1) er implementeret + bygget** (md5
`409af875`, `retired`-felter, `release_buffer`, slet aldrig busy buffer),
men **run 1 i probe2 reset STADIG** (98 s, present #100) med **0
retire-hændelser** i loggen — enten fanger `busy`-flaget ikke racen, eller
reset'et har en anden udløser. Probe2 kører 5×5 min med det rettede modul.

**Næste eksperiment (efter probe2):** `gfx.webrender.force-disabled=true`
i profilen (hard override) → sanity-tjek at siden stadig renderer (gamle
GL-layers-kompositor via EGL) → probe igen 5×5 min. Hvis reset'et forsvinder
uden WebRender, har vi en brugbar konfiguration til spillet.

## Opdatering 23:34 — force-disabled virker ikke; Firefox 128 har kun WebRender

- `gfx.webrender.force-disabled=true` i prefs.js ændrede INTET: GPU-processen
  har stadig WR*-tråde (WrGlyphRasterizer, WRWorkerLP#0-7), og reset skete
  igen (~45 s, 1 DeviceReset) → canvas sort igen (mean 4). **Firefox 128 ESR
  har ikke længere den gamle GL-layers-kompositor: acceleration → WebRender-GL,
  punktum.** "GL-layers"-tilstanden vi testede = WebRender på OpenGL.
- **Probe2 med buffer-fixet (retired):** run 1 RESET (98 s, #100), run 2+3
  ingen reset, men run 4-5 blev ubrugelige: **Firefox' EGNE glxtest-processer
  gik i D-state** (samme fb-driver-vej som vores måle-læsninger), og WebGL-
  kontekstoprettelse fejlede derefter (`Exhausted GL driver options` →
  `FAIL_NO_CONTEXT`). Gentagne Firefox-opstarter forringer altså boksen
  (D-state-glxtest + load-stigning). Buffer-fixet eliminerede ikke reset'et
  og udløste ingen retire-hændelser.
- Efter genstart + re-init renderer en frisk kørsel korrekt indtil det flaky
  reset (~40-100 s, undertiden aldrig).

**Åbne spor videre:**
1. **Vendor-GL-fehl-tjek (standalone):** udvid test_client_x11 med
   `glGetGraphicsResetStatus`/`glGetError` hver frame over 300+ frames — hvis
   vendor-GL melder sporadiske fejl, er reset'et vendor; hvis altid NO_ERROR,
   er det Firefox/WebRender-intern.
2. **Tight watch + gdb** på GPU-processen i reset-øjeblikket.
3. **Firefox-kilde-analyse** af `WR_POST_UPDATE`-detektionen (hvad præcis
   udløser UNKNOWN-reset).
4. **Auto-genstart-workaround** (spil i bidder).

## Status i ét blik

- **`layers.acceleration.disabled=false` + `gfx.webrender.enabled=false`
  (gamle GL-layers-kompositor via EGL) får spillet til at animere** — modsat
  Basic-kompositoren, hvor Subway Surfers frøs efter første frame (0 fps,
  clock_gettime-storm i content-main ~3,8 kHz + SoftwareVsyncThread-spin).
  Med GL-layers: GPU-proces kører, present-tæller vokser (#900+), vinduets
  pixels animerer (70–168k px ændret pr. 2–4 s).
- **MEN præsentationen til skærmen knækker efter GPU-proces-genstart:**
  ~50 frames inde logges `DeviceResetReason::UNKNOWN WR_POST_UPDATE`, GPU-
  processen genstarter, wrap-genoptages på samme vindue (0x1000057), og de
  nye presents opdaterer KUN vinduets egne pixels — X-root og fb0 forbliver
  statiske (kun panelets ur). Sessionen dør senere
  (`CompositorBridgeChild ... AbnormalShutdown`, `accel canvas lost`,
  channel error), og 3. kørsel tog hele BOKSEN ned (genstart).
- **Efter reboot + genkørte patches virker GL-layers end-to-end for den
  simple side:** WEBGL_RESULT OK, og animationen når root (518.481 px/2 s)
  OG fb0 (329.280 px/2 s). Så skærm-stien virker — den knækker kun i
  spil-sessionen efter GPU-reset.

## Hvad vi ved om den sorte skærm (målt)

- Vindue 0x1000057 (EGL-barn, depth 32): animerer (XGetImage viser spillet).
- Navigator 0x100003c (depth 32): XGetImage inkluderer barnet — spillet.
- Openbox-ramme 0xe006f9 (depth 32): SORT i indholdsområdet.
- X-root (depth 16) + fb0: statisk/sort i vindueområdet.
- `x32probe1–4` (repoet) beviser at X-serveren KAN kompositerer 32-bit
  vinduer/children til root OG fb0: openbox-framet, visual 0x1ec (Firefox'),
  separat X-forbindelse som tegner, XSync efter hver frame, gentagne frames
  (grøn/blå skift) — ALT virker isoleret. Fejlen er altså Firefox/GPU-
  genstart-specifik, IKKE en server-begrænsning. (Uafklaret.)

## Tilføjelse efter run 4+5 (samme nat)

- **Frysen kræver IKKE GPU-reset:** i run 4 (snapshot hver 4 s over 5 min)
  kom spillets første frame på root kl 20:26:40–44 (126.416 px ændret i
  vindueområdet) — derefter 0 ændringer, UDEN DeviceReset (grep = 0) og kun
  ÉN wrap ("pakket ind" = 1). Present-kæden sænkede til ~5 fps (50 presents
  pr. 10 s; loggen skriver hver 50.) og stoppede til sidst helt.
- **Vinduet animerer mens skærmen er frossen:** målt i run 5: vindue 72.033
  px/4 s ændret, X-root 49 px/4 s (kun uret). GPU-processen var i live og
  travl (72–91 % CPU) — den spinner/arbejder, men præsenterer ikke til
  skærmen.
- **Boksen gik ned to gange** under spil-kørsler (strøm-cykling; samme
  mønster som body-rød-testen — ingen panic-log). Stop spillet ved høj load
  (`pkill -9 -x firefox-esr`) i stedet for at vente.
- stall_capture.sh detekterede "stall" for tidligt (log-flush: talte kun 2
  present-linjer mens loggen faktisk havde #150); GPU-proces-lookup fejlede
  (`pgrep -f contentproc` + `grep " gpu$"` matchede ikke) — ret begge før
  brug. gdb-attach på main-processen virkede tidligere fint på boksen.
- `xrefresh` findes ikke (x11-utils ikke installeret) — kunne ikke teste om
  tvungen Expose får indholdet frem. Installér ved lejlighed.
- **Efter genstart + genkørte patches virkede GL-layers end-to-end for den
  simple side igen** (root 518.481 px, fb0 329.280 px ændret/2 s) — så
  stakken er sund; fejlen er spil-specifik.

## Næste skridt

1. **Lokal WebGL-stress-side** (ingen reklamer/Unity/netværk): kontinuerlig
   rAF-animation, fuld canvas, FPS via dump, justerbar opløsning. Kør 5+ min.
   Virker skærmen hele tiden → poki/Unity-specifikt; tier den → vores
   present-sti. (Plan godkendt af bruger; side endnu ikke bygget.)
2. **uBlock Origin** i profilen som kontrol (brugerens forslag; reklamer er
   ikke årsag til frysen, men slider på ressourcerne og kan øge reset-risiko).
   Installeres i /home/kristian/ffprof mens Firefox er stoppet.
3. **gdb på GPU-processen** når present-kæden sænker farten — find den
   blokerende tråd (gralloc-lock? XSync? buffer-tømning?). Ret
   stall_capture.sh's GPU-lookup først.
4. Installér `xrefresh` (x11-utils) og test om tvungen skærmopdatering får
   indholdet frem (server-redraw-fejl → workaround).

## Tilføjelse efter run 6 (stress-siden, 25. aug nat, 21:20–21:45)

**Stress-siden er bygget og kørt — fejlen er REPRODUCERET uden Unity/reklamer/
netværk.** Dermed er hovedspørgsmålet fra planens trin 1 besvaret: det er IKKE
poki/Unity-specifikt; det er vores Firefox/GL-layers-present-sti.

Målt (live-aflæsninger under kørslen; bevisfiler gik tabt ved genstart, /tmp =
tmpfs på denne boot):

- `webgl_stress.html` kørte fra kl 21:25:01 med GL-layers
  (`?scale=1&tiles=32&tex=0`, vindue 1280x814 canvas, 1280x948 surface).
- De første 4+ min opdaterede skærmen KONTINUERLIGT: root ~1.040.000 px/4 s,
  fb0 ~437.000 px/4 s, vindue ~1.050.000 px/4 s (fuld canvas ændres hver
  frame). Present-kæden kørte (#2 → #50 → #150), rAF-FPS ~1,4–1,8 (sænket
  present-rate, men ingen frys).
- Kl ~21:26:58 gik det i stå: snapshot-loopen stoppede — `/tmp/xdump`
  (XGetImage på root) og `dd if=/dev/fb0` gik i **D-state (uafbrydelig)**,
  ligesom et tab-barn. GPU-processen spindede op til ~100 % CPU.
- Load steg 1,8 → 9,2 → 13,4. `pkill -9 -x firefox-esr` + `pkill -9 -f
  "/usr/lib/firefox-es[r]/"` stoppede GPU-processen, men D-state-processerne
  overlevede — **sysrq-b-genstart nødvendig** (samme strøm-cykling-mønster
  som spil-kørslerne).
- Efter genstart: VT=tty7, HDMI=1, patches + `sh /root/gpu_up.sh` genkørt,
  værktøjer genopbygget, `xrefresh` (x11-xserver-utils) installeret.

**Nyt spor:** XGetImage og fb0-læsninger blokerer i kernen når present-kæden
kiler — mistænkt serialisering i X-server/fb-driver omkring store XPutImage-
presents, ikke en X-server-begrænsning (x32proberne kørte isoleret fint).

**Værktøjer opdateret:**
- `webgl_stress.html` (ny): fuld canvas, rAF, FPS via dump(), params
  `scale`/`tiles`/`tex`, logger `CONTEXT_LOST`/`RESTORED`.
- `capture_stress.sh` (ny): tidslinje root/fb0/vindue hver 4. s med
  rootdiff-diffs + present-nummer + DeviceReset-tæller.
- `rootdiff.c`: nu 16/32-bpp (vindue-diff).
- `stall_capture.sh` (rettet): GPU-lookup via `ps -eo pid,args | grep " gpu$"`
  (pgrep-cmdline matchede ikke), stall-detektion via HØJESTE present-nummer
  (log-flush-robust) + rate-check < 1 present/s over ~15 s. Fejl rettet under
  kørsel: `grep -oE "[0-9]+"` fangede også "11" fra "x11ws" → nu
  `[0-9]+$`-anker.

**Opdateret plan (fuld version — inkl. de sider der blev aftalt i sidste
session, men manglede i dokumentationen):**
1. ~~Lokal stress-side~~ — **FÆRDIG:** fejlen reproduceres; present-stien er
   synderen (ikke poki/Unity). Gentag evt. med `tex=1` og/eller `scale=1,5`
   for at finde belastningsgrænsen, og kør `stall_capture.sh` (nu rettet)
   samtidig for at få gdb-backtrace i øjeblikket hvor XGetImage/fb0 går i
   D-state.
2. **uBlock Origin** i profilen som kontrol (ikke årsag, men slider på
   ressourcerne og kan øge reset-risiko). Installeres mens Firefox er stoppet.
3. **gdb på GPU-processen** ved stall (fixed stall_capture.sh) — find den
   blokerende tråd (gralloc-lock? XSync? buffer-tømning? XPutImage-serialisering?).
4. **xrefresh-test** (installeret): kør `DISPLAY=:0 xrefresh` under en
   kørende stress-side og se om tvungen skærmopdatering får indholdet frem —
   det ville pege på server-redraw-fejl og give en workaround.
5. **Andre WebGL-sider som andet datapunkt** (aftalt i sidste session, manglede
   i notaterne):
   - **Shadertoy** (shadertoy.com) — pure fragment-shaders, anden kodevej end
     Unity, ingen reklamer; god til at adskille tung fragment-belastning fra
     Unity-engine-problemer.
   - **WebGL-aquarium / three.js-eksempler** (fx webglsamples.org/aquarium,
     threejs.org/examples roterende geometri) — mange draw-calls, kontinuerlig
     animation, ingen annoncer.
   - **Basemark WebGL** — rigtigt benchmark med flere scener, men for tungt
     for boksen (nedbrudsrisiko) → gem til sidst.
   - (Voxel Space/A-Frame og Khronos conformance kun hvis nødvendigt.)

## Kommandoer der virker (efter genstart, root)

```bash
# laptop:
bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
bash devuan/gpu/eglplatform_x11/patch_driver_minor.sh
# boks:
sh /root/gpu_up.sh
# værktøjer (tmpfs — genopbyg efter reboot):
scp devuan/gpu/eglplatform_x11/{start_game.sh,capture_game_black.sh,xdump.c,x32probe*.c} root@192.168.0.188:/tmp/
# boks: gcc -o /tmp/xdump /tmp/xdump.c -lX11 (og x32probe*)
```

Profilen er i GL-layers-tilstand (acceleration default = til, webrender
false, gpu-process true). Profil-preferences kan blive omskrevet ved
Firefox-exit (fx `layers.acceleration.disabled=false` forsvinder — det ER
default), så tjek `prefs.js` efter behov.
