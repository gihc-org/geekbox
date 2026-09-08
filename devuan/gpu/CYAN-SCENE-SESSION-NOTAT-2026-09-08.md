# Cyan-scene-sporet — session-notat 8. sep 2026 (drawBuffers, instanced-sti, buffer-snapshots)

> Agent/model: [codex:deepseek-v4-flash].
> Fortsætter `CYAN-CLONE3-SESSION-NOTAT-2026-09-06.md` + `CYAN-SCENE-HANDOVER-2026-08-27.md`.

## Fortsættelse — ny session 8. sep aften (efter commit dbbe440)

> Agent/model: [codex:model-ukendt] (bekræftes ved commit — ikke arvet fra
> tidligere sessioner).
> Boks 1 verificeret OPPE (20:22): IP 192.168.0.171, kernel 3.10.0, Xorg fra
> 17:28, pvrsrvkm indlæst + logd/servicemanager kører, bindapi bind-mount
> intakt (md5 467debb…), `/dev/sw_sync` 0666, ur korrekt → bring-up fra 17:28
> er ikke rørt.

## Status i ét blik (20:2x)

- **Nyt vnext5 (md5 `6c1626e3…`) bygget + installeret 20:26** med
  SHADOW-draw-probe: efter et stort draw (big ≤ 40, derefter hver 250.) køres
  samme draw IKKE-instanced og read-framebufferen aflæses mod basen fra
  postdraw-prøven (base A = efter det rigtige draw). Skriver skyggedrawet
  pixels (changed>0), gentages også det rigtige instanced-kald (C vs A).
  Ekstra state-log: rasterDiscard, stencil, VAO, ARRAY/ELEMENT-buffer.
  Backup: `/root/egl_proxy_631f9a40.so.bak`.
- Evidens-gennemgang af tidligere log: **alle store draws er verdens-draws**
  (prog 7/10/22/4, primcount=1, rfbo=3, 836x470) — q37/q38 = big-kald 16/17.
  Shadow-probe på de første 40 store draws rammer derfor verdens-draws.
- **Næste:** ren profil → Firefox-kørsel med vnext5 → aflæs SHADOW-linjerne;
  derefter præsentations-test med alpha-shim (kørsel 2).

## Checkpoint 20:30-20:45 — vnext5-kørsel (kørsel 10, ren profil, loadede)

- **SVAR på shadow-spørgsmålet:** verdens-draws (prog7/10/22, big 1-40 +
  periodisk til big 5000, 60 SHADOW-målinger) skriver **0 ændrede pixels,
  også når samme draw køres umiddelbart igen IKKE-instanced i live-
  konteksten** (changed=0, err0/err1=0x0) → live-kontekstens draw-kald er
  brudt UAFHÆNGIGT af instancing (primcount=1, divisor=0).
- **Probe-metode valideret live:** prog4 (loading/attract-UI, count=2040,
  big 13+28) — base nonsky=61055 → skyggedraw changed=8.977 px (nonsky
  61.475) og instanced-gentagelse skrev også (changed-vs-base=8.977, nonsky
  61.613) → ekstra-draw+aflæsning virker.
- **Ny state-forskel (kandidat):** UI-18-verts/prog4-draws kører FØR
  verdens-draws med depthtest=0, depthmask=1, blend=1; verdens-draws har
  depthtest=1 LEQUAL, depthmask=1, blend=0, cull=BACK; clear
  mask=0x4100/0x4500 + clearDepthf=1 hver frame. Hvis 1.5 alligevel skriver
  depth ved depthtest=0+depthmask=1 (eller UI tegnes efter clear med depth≈0),
  fejler verdens-draws LEQUAL mod depth-bufferen. rasterDiscard=0,
  stencilTest=0, vao=3, drawBuffers=[0x8ce0,0,0,0] — alt afskrevet.
- [udført] Evidens gemt: boks `/root/cyan_draw_probe_vnext5_20260908.log` →
  laptop `/tmp/cyan_draw_probe_vnext5_20260908.log`. Firefox lukket pænt
  (SIGTERM, ingen SIGKILL).
- [aftalt] **Næste (vnext6):** variant-shadow-draw med depth test FRA
  (derefter cull FRA hvis uændret) på verdens-draws → afgør om depth-buffer
  (eller face-culling) er porten live. Derefter præsentations-test (kørsel
  med alpha-shim).

## Checkpoint 20:36-20:45 — vnext6-kørsel (kørsel 11): DEPTH-TEST ER PORTEN

- **vnext6 (md5 `51ed38ec…`) installeret 20:34**; kørsel 11 loadede; 40+
  SHADOW-linjer med var-depthoff/var-culloff.
- **Mekanisme bekræftet live:** med DEPTH_TEST FRA rasterserer verdens-draws
  live: prog7 in-frustum (fx 1515 v = 75.344 px, 2961 v = 203.364 px, 6273 v
  = 298-341.856 px), prog10 (564 v = 19-107 k px, bånd), prog22 (4848 v =
  10.262 px). Med depth/cull FRA var prog7 stadig 0 på de to første prog7-
  draws (q9/q10-analoge = clip-space UDENFOR frustum pr. tidligere analyse —
  korrekt 0). GL-fejl hele vejen: 0.
- **Forklaring:** UI/2D-draws (prog4, depthtest=0) kører FØR verdens-draws
  og har depthmask=1; clear sker med clearDepthf=1 + mask 0x4100. Hvis 1.5-
  driveren skriver depth selv med depthtest=0+depthmask=1, fylder UI depth
  med ≈0 → verdens-draws (LEQUAL) fejler næsten alle (kun 1-2 px passerer,
  målt #33/#34). Offline-replay klarede sig fordi depth blev ryddet før draw.
- [udført] Lydfælde gentaget efter Firefox-lukning: hængt ALSA-strøm
  (owner 1699 død, delay −12,3 M frames) → `kill -9` pulseaudio (20:35),
  status "closed", ny pulseaudio autospawnet.
- [aftalt] **Næste (vnext7):** proxy-lap sætter glDepthMask(GL_FALSE) under
  alle draws med depthtest=0 + depthmask=1 (gendannes bagefter) → verden skal
  kunne tegne med normal LEQUAL mod den ryddede depth. Log: depthmask-lap +
  fortsat postdraw/SHADOW. Venter: hvis pre-post-diff>0 på verdens-draws →
  fix + derefter præsentations-test med alpha-shim.

## Checkpoint 20:45-20:55 — vnext7/vnext8 + gameplay (bruger i loop)

- vnext7 (1ada6701) crashede ~40 s inde (channel error, ingen kernel-fejl,
  minidump-generation fejlede) — kendt skrøbelighed; depthmask-lap var aktiv
  (kald 1-73 på UI-draws) men big1-4 viste stadig 0 px → lap alene utilstræk-
  kelig (clear rammer fbo=3 korrekt, men depth alligevel ikke fri).
- **vnext8 (20763b13): tvungen depth-clear (CYAN_DEPTHCLEAR=1) før hvert
  stort depth-on-draw virker DELVIST:** loading/attract-verdens-draws skriver
  live via RIGTIGE draw-kald (nonsky 43.556 → 92.888 → 199.847 → 334.169 →
  371.763; prog7 big16 +69k px). Kørsel stabil (big 30.500+, ingen crash).
- **Bruger (20:5x): klikkede "Press to play" → HUD/pause/point synlige, men
  verden stadig overvejende cyan:** kun "små felter" + én gang et større
  område renderet; drengen aldrig set → gameplay-verdens-draws har (stadig)
  en port ud over depth (force-clear forud for hvert draw slog ikke til i
  gameplay). pre-post-diff=0 ved store draws i gameplay; ns=43.556 statisk.
- [udført] Evidens: `/root/cyan_draw_probe_vnext8_gameplay_20260908.log`.
- [aftalt] **vnext9 (eae48aa5):** samme lap+force-clear, men SHADOW-probe med
  var-depthoff/var-culloff AKTIV (CYAN_SHADOW_PROBE=1) og probe-loft hævet
  (400 → dækker gameplay) → svarer om gameplay-draws rasterserer med depth
  fra, eller om en anden port (fbo/attachment/program-tilstand) er aktiv.
  Bruger trykker play ved "Press to play".

## Checkpoint 20:55-21:15 — PRÆSENTATIONSSYMPTOM (4. gang) + shim-test

- **vnext9 gameplay-data (big 3.250-4.250):** rigtige draws skriver stadig 0
  trods force-clear; var-depthoff skriver (prog7 6273 v = 341.918 px, prog10
  49.332 px, prog22 6.160 px) → gameplay-verdens-draws har stadig depth som
  port (clearDepthf=1 forbliver; glGetFloatv(DEPTH_CLEAR_VALUE)=0 i gameplay
  — driver-quirk). Loading skrev med force-clear, gameplay gør ikke → åbent.
- **Præsentationssymptom gentaget (4. gang, 21:0x)** ved play-start, også med
  BiDi-preload-shim AKTIV: loseContext blokeret + getContext hooked (bevist i
  cyan_game.log), men INGEN "ALPHA-SHIM: alpha tvunget true"-linjer → alpha-
  forcing IKKE bekræftet → shim-test foreløbigt inkonklusiv.
- **DOM-check under symptom (tidligere kørsel):** canvas 836x470, display
  block, visible, opacity 1 — canvas forsvinder IKKE fra DOM'et.
- **Bruger (21:1x): ser poki-baggrund + ikoner til andre spil** = spil-canvas-
  laget slippes af compositoren; poki-sidens indhold bagved skinner igennem.
- **fb0-bevis (21:08):** skærm næsten statisk (2 px forskel over 2 s) trods
  JS-draw-aktivitet (#13.200 drawElements i preload-tæller) — GL kører, men
  intet præsenteres. Råbilleder `/tmp/fb_sym{1,2}.raw` (1920x540 synligt,
  RGB565) + konverterede PNG'er på laptoppen (fb565→PNG via PIL; RGB;16/
  BGR;16 varianter).
- **Fælde bekræftet:** Firefox BiDi tillader KUN 1 aktiv session; død klient
  efterlader sessionen hængende ("Maximum number of active sessions") →
  eneste pålidelige frigørelse er Firefox-genstart. domcheck under symptom
  kræver frisk kørsel.
- **Åbent:** (a) bekræft canvas-attrs i shim-kørsel (alpha:true?) og tving
  alpha hvis getContext-stien ikke fanges; (b) gameplay-depth: hvorfor fejler
  verdens-draws mod depth trods clear — test depthfunc-ALWAYS/depth-off på
  rigtige gameplay-draws; (c) compositor-lag: præsentationen taber canvas ved
  play-start (software-layers/webrender-varianter).

## Status i ét blik

- **SLUTSTATUS 8. sep aften:** boks genstartet → IP 192.168.0.171 (bring-up
  færdig: ur, GPU, bindapi, sw_sync). Cyan-scene-sporet står ved en skarp
  modsigelse: offline-replay af rigtige data+shaders rasteriserer (q37/q38),
  men live skriver de samme draws 0 fragmenter. divisor=0, clearDepthf=1,
  drawBuffers/frustum/instanced-sti alle afskrevet. Næste live-test:
  shadow-draw-probe; præsentationssymptomet (spilområde forsvinder ved start)
  testes med alpha-shim/udvidelse. Firefox er lukket; proxy vnext4
  (631f9a40) installeret på boksen. Dokumentation + TODO-prompt opdateret;
  commit afventer model-tag.
- Boks 1 (192.168.0.142) kører stadig clone3-fix-kernen (3.10.0 #1 Sun Sep 6 22:12:06),
  ikke genstartet siden 6. sep — GPU/bindapi/sw_sync intakt; Firefox kørt 2× i dag.
- **drawBuffers-sporet er afsluttet:** store verdens-draws binder fbo=3, viewport
  836x470, scissor OFF, depth ON, cull BACK, `drawBuffers=[0x8ce0,0,0,0]` →
  attachment er IKKE problemet (målt på vellykket load+play, 158 postdraw-samples,
  alle ensartet himmel-cyan 130,255,238 / kant 147,235,234).
- **glDrawElementsInstanced-stien er frikendt:** scene_instanced_probe (samme
  prog7-shaders, EBO+VBO, primcount=1) tegner kvadet korrekt på 1.5-stakken
  (T2=T3=T4=T5, ~23.575 afvigende prøver, 0 GL-fejl) — også med cull/depth som
  spillet. Replay-proben før brugte kun glDrawArrays; nu er begge stier bekræftet.
- **glMapBufferRange virker på 1.5** (VBO+EBO, GL_MAP_READ_BIT, korrekte data,
  0 fejl) — glGetBufferSubData er NULL, men map-stien er brugbar til vertex-snapshots.
- **Ny proxy 63fe4c6c bygget+installeret** (8. sep 16:01): v715716d0 + VBUF/EBUF-
  snapshot via glMapBufferRange ved store draws (32 snaps, /tmp/vb_p*_q*.bin +
  /tmp/eb_p*_q*.bin) + pre/post pixel-diff (pre-post-diff=N/39 pr. stort draw).
  Backup: /root/egl_proxy_715716d0.so.bak.
- **Nyt load-stall (8. sep ~16:05):** anden Firefox-kørsel frøs ved 0% (poki) —
  kendt fælde efter gentagne genstarter. Brugeren lukkede Firefox.
- **Tredje kørsel frøs også ved 0% (8. sep ~16:12-16:14, proxy 63fe4c6c):**
  probe-loggen stoppede ved tidlig GL-init (11 linjer, kun 1x1-teksturer, INGEN
  store draws) → vnext's nye kode (buffer-snapshot + pre/post-readback) nåede
  ALDRIG at køre (kræver count≥512). cyan_game.log: "WebGL context was lost"
  (poki linje 61); dmesg: ingen PVR-fejl. Profil-cache/cookies/storage ryddet
  (8. sep ~16:16) — prefs.js urørt. Næste forsøg efter længere pause.
- **KØRSEL 4 LOADEDE (8. sep 16:25, ren profil + proxy 63fe4c6c):** 32 buffer-
  snapshots (vb/eb .bin), 94 postdraw-samples — ALLE pre-post-diff=0. Spillet
  viste cyan-skærm med "tryk på en tast for at start"; ved start forsvandt
  spilområdet fra siden (præsentations-/compositor-symptom; shim/alpha-fix var
  IKKE indlæst denne kørsel).
- **Offline clip-space (rettet attribut-rolle):** position ligger i loc 1
  (buf 3, a1), normal i loc 0 (a0). Store prog7-draws q37/q38/q39/q65/q66 har
  geometri 100% inden for frustum (fx q38: x[-0.09..0.25], y[-0.73..-0.42] =
  bane/grund i nederste midte); q19/q41 delvist; q9/q10/q33/q61 udenfor.
- **REPLAY MED RIGTIGE DATA (scene_real_replay.c, ~16:30):** q37+q38 afspillet
  offline på 1.5 med spillets shaders + fangede VBO/EBO + matricer:
  q38 → 7.218 ændrede pixels, q37 → 72.945 pixels; instanced og ikke-instanced,
  med og uden spillets depth/cull-tilstand → **data+shader+draw-sti kan tegne**.
- **DIVISOR-HYPOTESE BEVIST (mekanisme, ~16:32):** med glVertexAttribDivisor
  (pos, 1) efterladt giver præcis samme q38-replay **0 ændrede pixels** (også
  ikke-instanced glDrawElements) — kollaps til vertex 0 ved primcount=1.
  → næste måling: divisor-tilstand i live-draws (proxy vnext2 logger divisor
  pr. attrib + vertexAttribDivisor-kald + fuld-frame pre/post-diff for første
  10 store draws).
- **KØRSEL 5 (vnext2 e4e4ca74, 16:46:13) crashede ~30 s inde** ved første
  store draw (q8): loggen stopper lige efter EBUF-SNAP q8, ingen postdraw/
  FULL-POST-DIFF. divisor-log virker (31 linjer, alle divisor=0 i de tidlige
  UI-draws; ingen vertexAttribDivisor-kald før crash). Profilen var BESKIDT
  (kørsel 4 SIGKILLet 16:34 uden oprydning) → mønsteret gentager sig:
  SIGKILL → næste kørsel fejler tidligt; ren profil → load virker.
  Profil ryddet igen 16:47:38; næste forsøg med vnext2 efter pause.
- **Ny lydfælde (8. sep ~16:03-16:07):** den hakkende lyd startede ved SIGKILL af
  den første (vellykkede) Firefox og fortsatte under den næste kørsel — pulseaudio
  (kørt siden 6. sep) sad med en hængt HDMI-strøm efter Firefox-dødsfaldet
  (`/proc/asound/card0/pcm0p/sub0/status`: RUNNING, delay ≈ -29,6 mio. frames).
  Fix: `kill -9 <pulsepid>` → ALSA `closed`; ny pulseaudio autospawnes (stille).
  Forebyggelse: dræb/ryd pulseaudio-strømmen FØR genstart af Firefox (start-
  scriptets `pkill -9 -x firefox-esr` kan efterlade en hængt strøm), eller mute
  sinken under målekørsler.

## Aftaler og beslutninger

- [udført] drawBuffers-måling på vellykket load+play (8. sep 15:53-16:00): se
  status — attachment-forklaringen er udelukket; næste spor er vertex-data/frustum.
- [udført] Instanced-sti-test (scene_instanced_probe.c, 8. sep ~15:57): virker
  på 1.5 → instanced draw-kald i sig selv er ikke roden.
- [udført] glMapBufferRange verificeret (samme probe) → brugt i ny proxy 63fe4c6c.
- [udført] Proxy vnext (63fe4c6c) bygget + installeret med buffer-snapshots og
  pre/post-diff; loggen nulstilles ved start_cyan_probe.sh. Backup af 715716d0.
- [afventer] Næste load-forsøg (efter kølepause 5+ min og evt. ren profil):
  fang VBUF/EBUF-SNAP + pre-post-diff på en vellykket load, træk /tmp/vb_*.bin +
  /tmp/eb_*.bin + log, og regn clip-space offline med de loggede matricer
  (frustum/NaN/degenerate-geometri).
- [udført] Clip-space-analyse + replay med rigtige data: geometri i frustum
  rasteriserer offline; divisor=1-kollaps giver 0 pixels → stærkeste kandidat
  for live-fejlen.
- [afventer] Proxy vnext2 (e4e4ca74) bygget: divisor-log + vertexAttribDivisor-
  hook + fuld-frame pre/post-diff (første 10 store draws). Installeres ved
  næste Firefox-kørsel; hvis divisor=1 findes på pos (eller andre enabled
  attribs) i store draws → rodårsag fundet.
- [udført] vnext2 crashede 2/2 ved første store draw → fuld-frame pre/post-diff
  fjernet (vnext3 746522ec, stabil i kørsel 7). vnext4 (631f9a40) = vnext3 +
  nonsky-pixel-tælling på allerede læst buffer + clearDepthf-hook +
  divisor-log i attrib-dump; stabil (kørsler 8-9).
- [udført] KØRSEL 9 (18:01, ren profil): loadede til "Press to play"; ved
  tast-start forsvandt spilområdet fra siden IGEN (præsentation). GL fortsatte
  med at tegne efter start (draw-seq 61.390+), men postdraw viste fortsat
  sky-grid + statisk nonsky=43.556 → verdens-draws når ikke fragmenterne live.
  prog7-FS har ingen discard (sort/0-alpha-output ville have øget nonsky).
- [aftalt] Stop for i aften (bruger, ~18:3x): dokumentation + commit + god
  start-prompt i TODO. Næste session: shadow-draw-probe live + alpha-shim-
  præsentationstest.
- [afventer] Hvis pre-post-diff=0 for alle store draws men bufferne indeholder
  gyldig geometri i frustum → driver-/shader-problem i 1.5 (jf. [åbent] 6. sep).

## Nøglekommandoer (boksen)

```bash
# build proxy (kode i /root/egl_proxy_vnext.c = repo egl_proxy.c):
gcc -shared -fPIC -Wl,-soname,libEGL.so.1 -o /tmp/libEGL_vnext.so \
  /root/egl_proxy_vnext.c -L/opt/hybris -Wl,--no-as-needed -l:libEGL_r.so -ldl
cp /tmp/libEGL_vnext.so /opt/hybris/libEGL.so.1.0.0
# lydfælde-fix (hvis hakkende lyd i loop efter stall):
#   ps aux | grep pulse  →  kill -9 <pid>   (ALSA-status går til "closed")
# probe (instanced + map-test):
gcc -O0 -g -o /root/scene_instanced_probe /root/scene_instanced_probe.c \
  -I/usr/local/include -L/opt/hybris -Wl,--no-as-needed \
  -l:libEGL_r.so -l:libGLESv2.so.2 -lhybris-common -landroid-properties \
  -ldl -lrt -lm
LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 \
  DISPLAY=:0 /root/scene_instanced_probe /root/p7.vs /root/p7.fs
```

Filer: `eglplatform_x11/scene_instanced_probe.c` (ny), `egl_proxy.c` (udvidet),
log/beviser lokalt: `/tmp/cyan_draw_probe_20260908.log` (715716d0-kørsel).

## Tilføjelser (efter reboot 17:20, boks IP 192.168.0.171)

- [udført] Bring-up efter reboot: clone3-kernen, GPU (insmod + gpu_up,
  pvrsrvctl-exit=0), /dev/sw_sync 0666, bindapi-lap genanvendt (ES-bind TRUE),
  ur synkroniseret fra laptop. Proxy vnext4 (631f9a40) bygget og installeret.
- [udført] **KØRSEL 8 (17:38, ren profil):** loadede til 74 % og frøs der
  (~17:41); Firefox stabil, ingen crash. Nye live-data:
  - `clearDepthf d=1` kaldt 6× → depth-clear er 1,0. (GL_DEPTH_CLEAR_VALUE-
    query returnerer driver-støj 7,1e-44 — driver-quirk, ikke reel værdi.)
  - **nonsky-tælling:** canvas indeholder 43.556 ikke-himmel-pixels (bbox hele
    rammen) under loading og 60.621 ved fryseren → de gamle 39-punkts grids
    var blinde; "ensartet cyan" var delvist sampling-artefakt.
  - Store draws giver stadig pre-post-diff=0 på grid-punkterne; draw-aktivitet
    stoppede ved fryseren (log står stille på 1.992 linjer).
  - vnext4 er stabil (86 VBUF-snaps, 28 postdraws, ingen crash).
- [udført] Kørsel 8 lukket + profil ryddet 17:57 (SIGKILL → profil-oprydning
  før næste load). Næste forsøg efter pause.
- [udført] **KØRSEL 9 (18:01, ren profil, vnext4): loadede til "Press to play".**
  Ved tast-start forsvandt spilområdet fra siden IGEN (samme som kørsel 4) —
  præsentations-/compositor-symptom ved gameplay-start (alpha-shim IKKE indlæst
  i disse kørsler). GL-siden fortsætter med at tegne efter start (draw-seq op i
  61.390+, prog7/10/22 store draws), men postdraw viser fortsat kun sky-grid +
  nonsky=43.556 statisk → verdens-draws skriver stadig 0 pixels til FBO'et live.
- [målt] prog7-FS (6_0.glsl) har INGEN discard; farve = texel.rgb × tint ×
  texel.a (alpha). Havde draws nået fragment-stadiet med sort/0-alpha-tekstur,
  ville nonsky-tællingen stige (sorte pixels) — den gør den ikke → geometrien
  rasteriserer IKKE til fragmenterne live.
- [åbent] Forslag til næste live-test (kræver proxy-byg + restart): "shadow
  draw" i proxen — efter det rigtige instanced-draw udføres et ekstra
  glDrawElements/glDrawArrays med samme tilstand og aflæses bagefter. Svarer på
  om live-kontekstens instanced-kald selv er brudt (modsat offline-replay).
- [åbent] Præsentations-sporet (spilområde forsvinder ved start): test med
  poki-fix-udvidelsen/alpha-shim indlæst under gameplay.
