# Firefox/FPS - session-notat 5. nov 2026

> Agent/model: `codex` / `deepseek-flash` (fra denne sessions egen
> `turn_context`). Fortsætter `docs/log/2026-11-04-firefox-fps.md` efter
> midnat. Branch: `firefox-fps-tuning`.

## Status i ét blik

- Reproduceret i en ny session: 575 matched transaction-par i spilprocessen
  har median **1,239 s**, p90 **1,418 s** og p99 **2,951 s**. Parent-processen
  har median **0,997 s**. Spilprocessen gennemførte 1,43-1,50 transactions/s
  fra 00:01-00:04 UTC. Den hardcodede backpressure står stadig på to
  samtidige transactions.
- Shutdown-dumpet fra den første Gecko-profil blev afkortet, fordi Web Content
  og Utility Process crashede under Firefox' shutdown med fejl i
  pthread-mutex. Runtime-dump via `SIGUSR2` er derimod verificeret med en
  komplet profil og bruges nu, så profilen kan hentes før browseren lukkes.
- Den komplette 5,5-minutters runtime-profil viser, at spillets rAF starter
  hver **0,699 s** i median. `CONTENT_FRAME_TIME` er **1,294 s**, mens
  `CONTENT_FULL_PAINT_TIME` kun er **66 ms**. Vsync tikker hver 33 ms, men
  Renderer-tråden bruger cirka **0,65 s CPU** på de store game-frames; 333
  composites blev sprunget over med `Too many pending frames`.
- `about:support` viser **`Compositing: WebRender (Software)`**. I et
  640x360-vindue steg skærmen til **5,0 opdateringer/s** og spillets rAF til
  **40-41/10 s (ca. 4,0 fps)**. Brugeren bekræftede, at spillet føltes
  markant bedre end tidligere. 1080p-baselinen var 1,4/s.
- 960x540 gav **3,2 skærmopdateringer/s** og **37-44/10 s rAF**, altså næsten
  samme spilfart som 640x360 med en større flade. SWGL med
  `gfx.webrender.software.opengl=true` var dårligere (2,2/s og 31-32/10 s).
- Ren gameplay-baseline: **1,4 skærm-opdateringer/s** og **1,3-1,4 rAF/s**.
- `layout.frame_rate=30` gav **1,3/s** og flyttede derfor ikke loftet.
- Tidsstemplet `nsRefreshDriver`-log viser cirka **18 ticks/s**, men næsten
  alle går i wait-mode: kun **1,33-1,35 completed transactions/s**. Median
  wait-mode er 0,65 s, og medianafstanden mellem completions er 0,74 s.
  Fra `Allocating transaction` til `Completed transaction` er medianen
  **1,398 s i spilprocessen** og **1,183 s i parent-processen**. De to
  samtidige transactions giver derfor en færdig frame cirka hver 0,74 s.
- S3 med canvas-halvering virker teknisk (`516x290` backing og
  `1031x580` CSS), men spillet bliver hængende på load-skærmen med
  `raf=300/10s` og `draws=0`, når skaleringen sker i preload. To kørsler,
  hvoraf den sidste fik næsten 160 s, viser samme resultat.
- Live-skalering efter stabil gameplay virker derimod: canvas blev
  `516x290` ved CSS `1031x580`, og spillet fortsatte synligt med draws og
  score. Den målte rate var dog **1,3/s** mod baseline 1,4/s, så
  canvas-halveringen giver ingen FPS-gevinst på dette sted i stakken.
- Firefox 140.12's `nsRefreshDriver` har en hardcoded grænse på **2**
  pending transactions; der findes ingen pref til at hæve den. Standarden er
  `layout.throttle_in_process_iframes=true` og `layout.throttled_frame_rate=1`,
  men `layout.throttle_in_process_iframes=false` gav også kun 1,3/s. Det er
  derfor ikke den almindelige in-process-iframe-throttling, der sætter loftet.
- Den uventede Firefox-afslutning under den tidsstemplede kørsel skete omkring
  den planlagte SIGTERM-oprydning. Der findes ingen ny minidump eller corefil,
  så et selvstændigt crash er ikke bevist.
- En fastlåst PulseAudio-buffer gav konstant dunkende lyd. Lydserveren blev
  genstartet; den kører nu uden sink-inputs. Brugeren bekræftede, at lyden
  stoppede.
- Boksen er ren ved sessionscheckpoint: Firefox, BiDi og testlyd er stoppet,
  og temperaturen er faldet til 74-77 °C.

## Erfaringer og konklusion

1. **Det gamle "frame-planlægningen kører for langsomt"-billede var for
   bredt.** En transaction bliver først completed, når compositor-parenten har
   behandlet WebRender-epoken og sendt `DidComposite`. Den nye profil viser,
   at Renderer-tråden bruger cirka 0,65 s CPU på de store game-frames.
2. **Content-processen er ikke flaskehalsen.** Dens paint-forberedelse er kun
   cirka 66 ms. Vsync kommer stabilt cirka 30 gange/s. Backpressure opstår,
   fordi Renderer ikke bliver færdig, før næste frame melder sig.
3. **Firefox kører WebRender i software.** `about:support` siger eksplicit
   `Compositing: WebRender (Software)`. Hardware-kompositoren er slået fra med
   `layers.acceleration.disabled=true`, mens WebGL stadig bruger PowerVR.
4. **Softwarekompositorens pris følger browserfladen.** Reduktion af hele
   vinduet virker, mens halvering af canvas-backingen alene ikke gør:

   | Vindue | Skærm | Spillets rAF | Bemærkning |
   |---|---:|---:|---|
   | 1920x1080 | 1,4/s | 1,3-1,4/s | nuværende baseline |
   | 960x540 | 3,2/s | 3,7-4,4/s | anbefalet balance |
   | 640x360 | 5,0/s | ca. 4,0/s | hurtigst, brugerbekræftet |

5. **SWGL gennem OpenGL-rendercompositoren hjalp ikke.** Med
   `gfx.webrender.software.opengl=true` faldt 960x540 til 2,2
   skærmopdateringer/s og 31-32 rAF/10 s. Prefen er rullet tilbage.
6. **Der er ingen skjult TV-opskalering.** XRandR og fb0 har kun 1920x1080-
   modes. Et mindre browser-vindue giver derfor ikke automatisk fuldskærm via
   HDMI-skalering.

## Aftaler og beslutninger

- [aftalt] Fortsæt Firefox/FPS-sporet med transaktionsprofileringen som næste
  undersøgelse; Servo er ikke et aktivt spor. (~00:57)
- [udført] Bokstilstanden blev kontrolleret ren: X på `vt8`, vnext12-proxyen
  matcher backup, bindapi-lappen er aktiv, GPU-modulet er indlæst, og der
  kørte hverken Firefox eller BiDi. (~00:57)
- [udført] En ny tidsstemplet kørsel blev startet med
  `MOZ_LOG=timestamp,nsRefreshDriver:5` og
  `MOZ_LOG_FILE=/tmp/nsrefresh_20261105_0100.log`; beviser samles i
  `/root/nsrefresh_pidpairs_20261105_0100`. (~00:58)
- [udført] Subway Surfers er indlæst korrekt på tutorial-skærmen med tog,
  skinner og `PRESS TO PLAY`. Et framebuffer-dump bekræfter, at spillet er
  synligt. (~01:00)
- [afventer] Brugeren skal fysisk starte spillet og spille i mindst 40 s, så
  transactionstiderne kan bekræftes i samme session. (~01:00)
- [udført] Transaction-parseren
  `devuan/gpu/eglplatform_x11/parse_nsrefresh_pairs.py` er tilføjet. Den
  matcher allocation/completion pr. proces, refresh-driver og
  transaction-id og rapporterer p50/p90/p99 samt rate pr. minut. (~01:06)
- [udført] Den nye transactionmåling er analyseret: 575 par i spilprocessen
  gav p50 1,239 s, mens parent-processen gav p50 0,997 s. Raten var
  1,43-1,50/s i den aktive periode. Spilprocessen loggede 568 gange
  `Hit max pending transaction limit` og 10.048 skipped paints. (~01:06)
- [udført] Testkørslen blev lukket pænt via BiDi; Firefox, BiDi og testlyd er
  stoppet, og temperaturen faldt fra 104-107 °C til 79 °C. (~01:07)
- [udført] Kildesporet i Firefox ESR 140 viser, at completion først sker, når
  compositor-parenten har behandlet WebRender-epoken og sendt
  `DidComposite` til indholdsprocessen. (~01:09)
- [udført] Gecko-profil nummer to er startet uden `MOZ_LOG`, med 5 ms sampling
  og shutdown-dump til `/tmp/prof_pidpairs_20261105_0120.json`; beviser
  samles i `/root/prof_pidpairs_20261105_0120`. (~01:11)
- [udført] Spillet er indlæst korrekt på tutorial-skærmen i profilkørslen;
  framebuffer-dump bekræfter tog, skinner og `PRESS TO PLAY`. (~01:13)
- [afventer] Brugeren skal spille i mindst 60 s, så profilens
  compositor-/IPC-markører kan analyseres. Profilkørslen må derefter kun
  lukkes pænt, fordi profilen ellers ikke skrives. (~01:13)
- [udført] Profilkørslen blev lukket via BiDi, men Firefox crashede under
  shutdown. Web Content fik en null-pointer translation fault efter
  `pthread_mutex_destroy`; Utility Process fulgte med
  `pthread_mutex_lock`. Der var ingen ny PVR/GPU-fault i `dmesg`. (~01:21)
- [udført] To nye minidumps og det delvise 12,6 MB profil-JSON blev arkiveret
  i `/root/prof_pidpairs_20261105_0120`. Den hængende crashreporter blev
  lukket efter arkiveringen. (~01:23)
- [udført] `salvage_gecko_profile.py` kan lukke et afkortet Gecko-profil-JSON
  syntaktisk, men den første profil nåede kun fire topniveauer og indeholder
  ingen tråde; den kan derfor ikke bruges til stackanalyse. (~01:24)
- [udført] Firefox 140's `SIGUSR2`-vej blev testet på `about:blank`: den
  skriver en komplet profil til `/home/kristian/Downloads/profile_0_<pid>.json`
  og lader browseren køre. Testprofilen er valid version 31 med 5 ms
  sampling og fem processer. (~01:29)
- [udført] `start_cyan_probe.sh` sender nu også
  `MOZ_PROFILER_STARTUP_INTERVAL`, `MOZ_PROFILER_STARTUP_ENTRIES` og
  `MOZ_PROFILER_STARTUP_FEATURES` videre gennem `env -i`. (~01:31)
- [udført] En ny spilkørsel med runtime-profil er startet i
  `/root/prof_sigusr2_20261105_0132` og står på tutorial-skærmen. (~01:32)
- [afventer] Brugeren skal spille i mindst 60 s. Derefter sendes `SIGUSR2` kun
  til Firefox' parent-process, profilen hentes, og først bagefter lukkes
  browseren. (~01:33)
- [udført] Runtime-profilen blev dumpet og valideret: version 31, 5 ms
  sampling, fem processer og 20 MB JSON. (~01:39)
- [udført] Profilanalysen viser 380 game-rAF-cyklusser. Startafstanden er p50
  0,699 s og p90 0,926 s. `CONTENT_FRAME_TIME` er p50 1,294 s og p90 1,509 s;
  `CONTENT_FULL_PAINT_TIME` er p50 kun 66 ms. Vsync er stabil ved cirka
  30 Hz. (~01:43)
- [udført] Renderer-tråden har 364 composites over 500 ms efter game-start;
  medianen for disse er 652 ms. Der er 44.504 samples inde i de lange
  composites mod 14.519 udenfor, og næsten ingen af samples'ne inde i de
  lange composites er tomgang. Flaskehalsen er derfor Renderer-arbejde, ikke
  vsync eller content-processens paint-forberedelse. (~01:45)
- [udført] 333 composites blev sprunget over med `Too many pending frames`.
  Firefox' `RenderThread::TooManyPendingFrames()` accepterer kun én pending
  frame og afviser nye, mens en frame build er i gang. (~01:46)
- [udført] `gfx.logging.slow_frames.enabled=true` er tilføjet med backup
  `/root/user.js.pre-slowframes.20261105_0140`. `bidi_cyan.py` har fået et
  `SUPPORT_POLL`-hook, som efter målingen navigerer til `about:support` og
  gemmer Firefox' ti største slow-frame-statistikker. (~01:49)
- [udført] Slow-frame-kørslen står klar på tutorial-skærmen i
  `/root/slowframes_20261105_0149`. (~01:50)
- [afventer] Brugeren skal spille i mindst 60 s. Derefter udløses
  `/tmp/support_now`, og de detaljerede transaction-, paint-, upload- og
  composite-tider hentes. (~01:50)
- [udført] `about:support` bekræfter `Compositing: WebRender (Software)`.
  `WEBRENDER` er `unavailable-no-hw-compositing`, fordi
  `layers.acceleration.disabled=true`. WebGL kører stadig på PowerVR Rogue
  G6110 med OpenGL ES 3.1 build 1.5. (~02:02)
- [udført] Slow-frame-prefen gav ingen `Slow Frame`-poster i denne
  WebRender-parent, sandsynligvis fordi game-pipelinen er root-bridge.
  Statussen er gemt i `/root/slowframes_20261105_0149/support.txt`; `user.js`
  er gendannet bit-identisk (`md5 0ac16be8…`). (~02:03)
- [udført] Automatisk 640x360-kørsel med `PLAY_AT`: vinduet blev faktisk
  640x360, skærmen målte **5,0 opdateringer/s**, spillets rAF var
  **40-41/10 s**, og draw-arbejdet var 76-78 ms pr. 120 frames. Brugeren så
  kørslen og bekræftede, at spillet føltes markant bedre. (~02:04)
- [aftalt] Test 960x540 som mulig mellemvej mellem hastighed og
  skærmstørrelse. (~02:05)
- [afventer] Resultatet af 960x540-kørslen `G7_960win_now`. (~02:05)
- [udført] 960x540-kørslen blev verificeret i et 960x540-vindue. Skærmen gav
  **3,2 opdateringer/s**, og spillets rAF var **37/10 s** i den afsluttende
  blok med toppe på **44/10 s**. Det er næsten samme spilfart som 640x360,
  men med 2,25 gange større flade. (~02:09)
- [udført] XRandR og fb0 tilbyder kun 1920x1080; der findes ingen 960x540-
  eller 1280x720-displaymode, som TV'et kan skalere op automatisk. (~02:10)
- [udført] `gfx.webrender.software.opengl=true` blev testet som SWGL med
  OpenGL-rendercompositor ved 960x540. Billedet virkede, men resultatet var
  dårligere: **2,2 skærmopdateringer/s** og **31-32/10 s rAF**. Prefen blev
  rullet tilbage, og `user.js` matcher igen `md5 0ac16be8…`. (~02:17)
- [foreslået] Gør 960x540 eller 640x360 til en dedikeret spilstørrelse.
  960x540 er den bedste balance; 640x360 giver den højeste målte rate.
  (~02:18)
- [aftalt] Dokumentér målingerne og skriv en ny handover-prompt før næste
  session. (~02:25)
- [udført] Ren baseline målt i aktivt gameplay med `POLL=20`. (~23:33)
- [udført] `layout.frame_rate=30` testet og `user.js` genskabt bit-identisk.
  (~23:43)
- [udført] `MOZ_LOG=timestamp,nsRefreshDriver:5` viste tick- og
  transaction-mekanismen. `timestamp` er nødvendigt; logfilen skal ligge i
  `/tmp` eller under `/home/kristian`, fordi Firefox ikke kan skrive i `/root`.
  (~23:53)
- [udført] Firefox/BiDi lukkes fremover via `CLOSE_WHEN_DONE=1`, som kalder
  `browser.close` fra den eksisterende BiDi-session. (~23:58)
- [udført] S3 bagudkompatibelt indbygget som `CYAN_CANVAS_SCALE` i
  `bidi_cyan.py`; standard er 1,0 og ændrer intet. (~23:58)
- [udført] S3 `0.5` testet to gange. Canvas blev `516x290` med CSS
  `1031x580`, men spillet gik ikke videre fra load-skærmen. Kørslerne blev
  lukket pænt, og lyd/processer er ryddet. (~00:08)
- [udført] Preload-skaleringen erstattet med `CANVAS_SCALE_POLL`, som skalerer
  canvas live via BiDi efter stabil gameplay. (~00:16)
- [udført] Live-skalering testet i aktivt gameplay: canvas gik fra
  `1031x580` til `516x290`, CSS-størrelsen blev bevaret, og spillet fortsatte
  med draws og stigende score. Skærmen gav **1,3 opdateringer/s** og rAF
  **1,3 fps** mod baseline 1,4/s. Negativt resultat; forsøget blev lukket
  pænt, og temperaturen faldt fra 104 °C til 82 °C. (~00:20)
- [foreslået] Undersøg Firefox' hårde grænse for pending transactions og
  relevante prefs, før S4 CPU/DDR-tuning forsøges. (~00:21)
- [udført] Firefox 140.12-kilden bekræfter, at pending-transaction-grænsen er
  hardcoded til 2 (`AtPendingTransactionLimit()` og
  `TooManyPendingTransactions()`). Der er ingen relevant pref at justere.
  (~00:25)
- [udført] De gemte tidsstempler parser nu også transaction-levetid:
  median **1,398 s** fra allocation til completion i spilprocessen og
  **1,183 s** i parent-processen. Backpressure-grænsen på 2 giver derfor
  kun en ny completion cirka hver 0,74 s. (~00:40)
- [udført] `layout.throttle_in_process_iframes=false` testet isoleret med
  backup og diff. Resultatet var 1,3 skærm-opdateringer/s og 1,3-1,4 rAF/s,
  altså ingen ændring. `user.js` er genskabt bit-identisk
  (`md5 0ac16be8…`). (~00:33)
- [udført] Testkørslen lukket pænt via BiDi. Firefox, BiDi og lydstreams er
  stoppet; temperaturen er faldet til 74 °C. (~00:33)
- [udført] Dokumentationsaudit: `OVERBLIK.md` peger nu på 5. nov-notatet og
  opsummerer de nye målinger. `TODO.md` har fået en ny NUVÆRENDE
  handover-prompt; 4. nov-prompten er markeret som afløst. (~00:39)
- [udført] Tre nye driftsfælder er dokumenteret som fælde 61-63:
  `MOZ_LOG_FILE`-rettigheder/timestamps, canvas-skalering i preload kontra
  live, og fastlåst PulseAudio-buffer efter hård Firefox-stop. (~00:39)

## Vigtige kommandoer

```bash
# Firefox
GAME_URL=about:blank CYAN_DEPTHCLEAR=1 \
  nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &

# BiDi med live-S3 og pæn lukning (send senere SIGTERM til kun BiDi-PID'en)
CANVAS_SCALE_POLL=/tmp/canvas_scale_now CLOSE_WHEN_DONE=1 \
  PYTHONUNBUFFERED=1 \
  nohup python3 /root/bidi_cyan.py preload goto \
  https://poki.com/en/g/subway-surfers 600 > /root/bidi_cyan.log 2>&1 &

# Først når gameplay er stabilt:
printf '0.5\n' > /tmp/canvas_scale_now

# Måling
/root/fb_fps /dev/fb0 40 20

# Tidsstempler til transaction-analyse (kun timing, ikke FPS)
MOZ_LOG=timestamp,nsRefreshDriver:5 \
MOZ_LOG_FILE=/tmp/nsrefresh.log \
  nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &

# Komplet Gecko-profil uden shutdown-crash
MOZ_PROFILER_STARTUP=1 MOZ_PROFILER_STARTUP_INTERVAL=5 \
  nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &

# Efter aktiv gameplay (Firefox-parentens PID):
kill -USR2 <Firefox-parent>
# Profilen lander i /home/kristian/Downloads/profile_0_<pid>.json

# Arealtest med automatisk spilstart
WIN="960 540" FBSEC=30 SETTLE=50 POLL=20 PLAY_AT="70,85,100" \
  bash /root/cyan_ab_run.sh G7_960win_now 115 "" ""
```

## Beviser på boksen

- `/root/nsrefresh_pidpairs_20261105_0100`: 575 transaction-par med
  p50 1,239 s i spilprocessen.
- `/root/prof_pidpairs_20261105_0120`: crash-minidumps og det afkortede
  shutdown-profilforsøg.
- `/root/prof_sigusr2_20261105_0132`: komplet 5 ms-profil, BiDi- og
  launch-logs.
- `/root/slowframes_20261105_0149`: `about:support` med
  `WebRender (Software)`.
- `/tmp/ab_G6_640win_now`: 640x360, 5,0 skærmopdateringer/s og 40-41 rAF/10 s.
- `/tmp/ab_G7_960win_now`: 960x540, 3,2 skærmopdateringer/s og 37-44 rAF/10 s.
- `/tmp/ab_G8_960_oglswgl`: negativ SWGL/OpenGL-test, 2,2/s og 31-32 rAF/10 s.
- `/root/nsrefresh_timestamp_evidence_20261104_2352`: tidsstemplede
  refresh-driver-logs.
- `/root/firefox_crash_20261104_2355`: Firefox-, BiDi- og dmesg-log fra
  oprydningen.
- `/root/bidi_cyan.py.pre-s3.20261104`: backup før S3-ændringen.
