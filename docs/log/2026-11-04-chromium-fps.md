# Chromium-sporet - session-notat 4. nov 2026

> Agent/model: `codex` / `deepseek-flash` (læst fra denne sessions
> `turn_context` i `~/.codex/sessions/2026/11/04/`). Oprindelig branch:
> `chromium-fps`; merget ind i `trunk` 4. nov 2026.
> Fortsætter handover-prompten i `TODO.md` fra 7. okt 2026.

## Status i ét blik

- Ny laptop virker med den kopierede SSH-nøgle. Boksen blev fundet på
  `192.168.0.135`; `pass` er ikke i PATH på den nye laptop, men var ikke
  nødvendig for SSH-adgangen.
- uBlock Origin Lite stod allerede på **Complete** og havde brede
  host-tilladelser. Det fysiske dashboard-klik var derfor ikke nødvendigt.
- Spillet kan loade gennem Poki med uBOL og nå titelskærmen. Et startklik kan
  trods `PokiSDK.commercialBreak()` føre videre til gameplay med lyd og
  stigende score.
- Firefox-vejen er igen aktiv og brugerbekræftet spilbar. Et skærmdump under
  gameplay viser drengen, skinnerne, togvognene og Halloween-scenen.
- Rendererens manglende 3D-verden er bekræftet som en depth-state-fejl.
  Med depth-test slået fra blev togvogne, skinner og bygninger synlige, men
  uden korrekt occlusion.
- `glClearBufferfv(GL_DEPTH)` fik Halloween-banen og titelskærmen til at
  rendere korrekt, men den eksperimentelle proxy gjorde senere GPU-processen
  fastlåst i `D`-tilstand. Den er derfor ikke en brugbar fix endnu.
- Renderer-crashene fortsætter. gdb gav en ægte V8/Maglev-backtrace, og
  `--no-maglev` samt `--jitless` fjernede dem ikke.

## Aftaler og beslutninger

- [udført] Ny laptops SSH-vej verificeret; boksen fundet på `192.168.0.135`.
  (~16:40)
- [udført] uBOL Complete og `<all_urls>`-tilladelse verificeret i Chromium-
  profilen. Dashboardet krævede kun genindlæsning efter extension-start.
  (~16:50)
- [udført] Den hardkodede nøglesti i `patch_android_bindapi.sh` blev erstattet
  med `SSHKEY`/`$HOME`, så scriptet også virker fra den nye laptop. (~17:47)
- [udført] `CHROMIUM_KIOSK=0` kan bruges til vinduestilstand. (~17:00)
- [udført] `CYAN_DEPTHOFF_TEST=1` fik verdensgeometrien til at rendere.
  (~17:13)
- [udført] gdb fangede SIGSEGV i `ThreadPoolForegroundWorker`; PC
  `0xade2665a` symboliserer til V8 Maglev
  `TaggedEqual::GenerateCode`. (~17:20)
- [udført] `--no-maglev` og `--jitless` blev testet; begge crashede stadig.
  (~17:26)
- [foreslået] Brug ikke den eksperimentelle `glClearBufferfv`-proxy i
  produktion, før GPU-wedge er løst.
- [udført] Kendt god EGL-proxy gendannes fra
  `/root/egl_proxy_chromium_2d66a43c.so.bak`, og boksen genstartes.
- [afventer] Efter software-reboot viser boksen desktop, men svarer ikke på
  `find_box.sh` via DHCP eller fallback-adressen. Brugeren bedes strøm-cykle
  boksen fysisk. (~17:39)
- [udført] Efter strøm-cyklus er boksen igen på `192.168.0.135`; ur, GPU,
  HDMI og bindapi-lap er gendannet. (~17:47)
- [afvist] `chromium_cdp.py poki` wrapper `window.PokiSDK`, men spillets
  modul har allerede en lokal SDK-reference. Det originale `commercialBreak`
  bliver derfor stadig kaldt. Et manuelt/CDP-startklik kan alligevel føre
  videre til gameplay. (~17:51)
- [udført] Gameplay verificeret med lyd, tutorial, kollisioner og stigende
  score, mens canvas fortsat kun viste baggrunden. (~17:52)
- [afvist] WebGL-prototype-hook rammer `clear(DEPTH_BUFFER_BIT)` og de store
  draw-kald (`count=5964`), men `clearDepth(1)+clear` ændrer ikke 3D-billedet.
  Den native `clearBufferfv`-test er derfor stadig ikke reproduceret i JS.
  (~17:59)
- [aftalt] Chromium parkeres midlertidigt. Firefox vnext12 er den aktive
  spilvej, fordi den er brugerbekræftet spilbar; Chromium-sporet genoptages
  kun med en afgrænset depth- eller JIT-reproduktion. (~18:03)
- [aftalt] Beviserne fra Chromium-sporet bevares: crashdumper, test-scripts,
  proxy-backups og denne sessionsnotat slettes ikke ved skiftet. (~18:03)
- [udført] Firefox vnext12 `dcc0a68f` + alpha-shim/BiDi +
  `CYAN_DEPTHCLEAR=1` startet. Skærmdump viser Halloween-banen og drengen
  korrekt; brugeren bekræftede, at drengen er synlig. (~18:05)
- [udført] Gameplay startet i Firefox. Skærmdump viser Halloween-scenen,
  drengen på skinnerne og score 129. (~18:06)

## Hvad vi ved

- **Poki-flowet kan passere.** Complete er aktivt, spilassets, geometrier og
  titelskærm loades. Et startklik udløser `PokiSDK.commercialBreak()`, men
  spillet kan alligevel gå videre til gameplay med lyd og score.
- **Depth-fejlen er reel.** Proxyens postdraw-probe målte
  `pre-post-diff=0` for alle store verdens-draws i normal tilstand: der blev
  sendt mange tusinde vertices, men ingen pixels ændrede sig. Med depth-off
  blev `pre-post-diff` 1-31 og scenen synlig.
- **Kun depth-clear med depth-write-mask og scissor genoprettet var ikke nok.**
  Baggrunden forblev synlig, mens geometrien blev testet væk.
- **`glClearBufferfv(GL_DEPTH)` ændrede resultatet markant.** Et direkte
  framebuffer-dump viste Halloween-titelskærmen med bygninger, skinner,
  graffiti og knapper. Efter efterfølgende kørsel gik GPU-processen i `D` og
  siden blev grå/brun, så metoden er ustabil.
- **JIT-hypotesen er udvidet, ikke bekræftet som eneste årsag.** gdb fangede
  Maglev, men `--no-maglev` gav SIGSEGV i et JIT-kodeområde, og `--jitless`
  gav SIGILL i Brotlis decode-sti. Der er altså mindst to crash-signaturer.
- CPU-temperaturen var 47-87 °C under testene og faldt til 59-67 °C efter
  crash. Termik er ikke forklaringen på renderer-crashet.

## Beslutning: hvorfor Chromium parkeres midlertidigt

Firefox er valgt som aktiv spilvej, ikke fordi Chromium er umuligt, men fordi
Chromium-sporet nu har flere samtidige fejl på en gammel, lukket driverstak:

1. **3D-renderingen er ikke korrekt.** Spillets logik, lyd og HUD virker, men
   verdensgeometrien bliver dybtestet væk. Depth-off viser geometrien, men
   ødelægger occlusion. Native `glClearBufferfv` renderede scenen korrekt én
   gang, men efterlod GPU-processen i `D`-tilstand. Den tilsvarende rene
   WebGL-løsning kunne ikke genskabes gennem browserens JavaScript-kontekst.
2. **Rendererens hukommelse er ustabil.** gdb fangede V8 Maglev ved
   `TaggedEqual::GenerateCode`; `--no-maglev` crashede i et JIT-område, og
   `--jitless` gav SIGILL i Brotlis decode-sti. Der finds også ældre SIGABRT
   med `stack smashing` og SIGSEGV i compositor-tråden.
3. **Enhver workaround rammer den næste fejl.** Depth-off giver synlige
   objekter, men forkert gennemsigtighed. `glClearBufferfv` giver korrekt
   scene, men GPU-wedge. JIT-fravalg fjerner ikke hukommelsesfejlene. Det gør
   Chromium-sporet til et dybt driver-/ANGLE-/V8-spor snarere end en
   browserkonfiguration, der kan færdiggøres i denne session.

Firefox-vejen har til sammenligning været brugerbekræftet spilbar siden 19.
sep 2026 med vnext12 `dcc0a68f`, alpha-shim via BiDi og
`CYAN_DEPTHCLEAR=1`. Den er langsom (ca. 2,6 fps ved 1080p), men den viser
spilleren, banen og øvrige elementer korrekt og kan bruges med lyd og input.

Chromium genoptages kun med et afgrænset mål, fx:

- en lille WebGL-test der reproducerer depth-clear uden GPU-wedge,
- en nyere PowerVR DDK/kernelvej, som ændrer den underliggende driver, eller
- en præcis rettelse til den konkrete V8/JIT-crash på denne ARM/kernel-kombination.

Indtil da er det mere nyttigt at forbedre Firefox-vejens fps end at bruge tid
på flere Chromium-kørsler med samme to blokader.

## Næste skridt

1. Løs depth-clear stabilt. Foretræk `glClearBufferfv` kun hvis GPU-wedge kan
   undgås; ellers sammenlign state lige før det første store verdens-draw med
   den fungerende Firefox-kørsel.
2. Kør den korrigerede 3D-vej med gdb og undersøg både Maglev/JIT- og
   Brotli-signaturerne. `--no-maglev` og `--jitless` er allerede afvist som
   selvstændige workarounds.
3. Hvis depth-wedge eller V8-crashene ikke kan isoleres, parkér
   Chromium-sporet og brug Firefox-vejen, som brugeren har bekræftet spilbar.

## Sikkerhedskopier og beviser

- Kendt god proxy:
  `/root/egl_proxy_chromium_2d66a43c.so.bak`
- Depth-off-testproxy:
  `/root/egl_proxy_depthoff_f41277b8.so.bak`
- Crashdumper gemt i `devuan/gpu/beviser/`:
  - `36a5471b-8a29-4f93-be29-6a112a72620f.dmp` (compositor SIGSEGV)
  - `76019387-eb7a-4763-93ea-e409de03f148.dmp` (SIGIOT, stack smashing)
  - `1ea92c63-db7f-4774-be06-ef68bc101e64.dmp` (gdb-fanget SIGSEGV)
  - `73e8e2cd-f703-44d6-aec5-c9bf3a3f54c6.dmp` (no-Maglev SIGSEGV)
  - `027d5e42-1749-45d1-ac44-452ed7ea1ecc.dmp` (JITless SIGSEGV)
  - `c5f38104-a8f1-44b1-a1c6-919008edc1db.dmp` (SIGILL i Brotli)

## Kommandoer der virker

```bash
bash devuan/find_box.sh
BOXIP=192.168.0.135 bash devuan/box.sh 'date -s @<laptop-epoch>'

# CDP-tunnel og midlertidig Python-venv på laptoppen
ssh -f -N -i ~/.ssh/geekbox_key -o ExitOnForwardFailure=yes \
  -L 9223:127.0.0.1:9223 root@192.168.0.135
python3 -m venv /tmp/geekbox-cdp
/tmp/geekbox-cdp/bin/pip install websocket-client minidump

# uBOL + CDP
CHROMIUM_KIOSK=0 CHROMIUM_LD_PRELOAD= \
CHROMIUM_EXTRA_FLAGS='--load-extension=/home/kristian/ublock-origin-lite|--disable-extensions-except=/home/kristian/ublock-origin-lite' \
CHROMIUM_URL=about:blank nohup /root/start_chromium_probe.sh \
  > /root/chromium_launch.log 2>&1 &

/tmp/geekbox-cdp/bin/python \
  devuan/gpu/eglplatform_x11/chromium_cdp.py poki \
  https://poki.com/en/g/subway-surfers 45
```

## Fælder

- CDP-siden kan hænge uden at Chromium er helt død. Tjek
  `/json/list`, renderer-processen og `gdb_renderer_5.log` før genstart.
- gdb-scriptet skal startes med `bash /root/capture_renderer_crash.sh ...` eller
  have eksekveringsret; `scp` bevarer den ikke nødvendigvis.
- `env -i` i startscriptet fjerner nye miljøvariabler, medmindre de sendes
  eksplicit videre.
- En `D`-tilstand i GPU-processen kan ikke afløses af et nyt Chromium-start.
  Gendan kendt proxy og genstart boksen.
- `/dev/fb0`-dumpet dækker kun 1920x540 og skal konverteres med højden 540.
