# Firefox/FPS-sporet - session-notat 4. nov 2026

> Fortsætter efter midnat i `docs/log/2026-11-05-firefox-fps.md`.

> Agent/model: `codex` / `deepseek-flash` (læst fra denne sessions
> `turn_context` i `~/.codex/sessions/2026/11/04/`). Branch:
> `firefox-fps-tuning`, oprettet fra `trunk` (`chromium-fps` blev merget ind
> 4. nov 2026).
> Fortsætter den spillbare Firefox-vej fra 19.-26. sep 2026.

## Status i ét blik

- Arbejdet er genoptaget 4. nov 2026 kl. 23:19. Boksen kørte rent uden
  Firefox/BiDi, havde korrekt DHCP-rute og DNS samt vnext12 `dcc0a68f` og
  GPU-modul indlæst.
- Første baseline-forsøg kl. 23:20-23:22 blev kasseret: brugeren så et sløret
  billede, og spilområdet forsvandt. Framebuffer-dumpet viste Poki-siden bag
  det manglende canvas-lag. Dette er det kendte
  compositor-/præsentationssymptom, ikke en brugbar FPS-baseline.
- Efter `service nodm restart` og et nyt forsøg uden automatiske klik står
  spillet korrekt på tutorial-skærmen kl. 23:27: Halloween-scenen, tog,
  skinner og `Press Arrow Key Right` er synlige. rAF var 14-15/10 s og
  WebGL-canvas `1031x580`. Temperaturen var 89 °C.
- Ren baseline målt i aktivt gameplay kl. 23:33:10-23:33:50:
  `fb_fps /dev/fb0 40 20` gav **1,4 skærm-opdateringer/s**. Spillets
  iframe-rAF gav **1,3-1,4 fps** (`raf=13-14/10s`), med 1.869-2.291 draws
  pr. 10 s og 46-58 ms samlet draw-tid pr. 10 s. Temperaturen var 92 °C.
- `layout.frame_rate=30` ændrede intet: 1,3/s mod baseline 1,4/s.
- Tidsstemplet `nsRefreshDriver`-log viser cirka **18 ticks/s**, men næsten
  alle går i wait-mode og bliver skippet. Spilprocessen får kun
  **1,33-1,35 completed transactions/s**; medianafstanden er 0,74 s, og
  median wait-mode er 0,65 s.
- Den sidste loggede spilkørsel endte med `Segmentation fault` i Firefox'
  indholdsproces og `Exiting due to channel error.`. Der var hverken OOM
  eller en ny PVR/GPU-fault i `dmesg`. En fastlåst PulseAudio-proces måtte
  dræbes, fordi den blev ved med at afspille den sidste lydbuffer.
- Firefox vnext12 `dcc0a68f`, alpha-shim via BiDi-preload og
  `CYAN_DEPTHCLEAR=1` er den aktive spilvej. Testkørslen er lukket ved
  sessionsslut, så næste session starter rent.
- Spillet er brugerbekræftet spilbart: dreng, tog, skinner og
  Halloween-scenen renderes, og lyd/input virker.
- En kort `fb_fps`-måling gav **0,6 skærm-opdateringer/s** i den mest aktive
  blok. Målingen er fra en allerede kørende/pauset session og er ikke en ren
  baseline.
- Temperaturen var **107 °C** under gameplay og 89 °C efter målingen.
  Kernen har termisk nedlukning ved 120 °C; køling skal følges.
- Chromium-sporet er parkeret. Lyd/logik kan køre, men 3D mangler, native
  depth-clear giver GPU-wedge, og separate V8/JIT-crashes består.
- Næste mål er FPS. Første test er `layout.frame_rate=30`, derefter
  `nsRefreshDriver:5`-timing og S3-canvas-nedskalering.

## Aftaler og beslutninger

- [aftalt] Chromium parkeres midlertidigt, og Firefox/FPS-sporet bliver den
  aktive vej. Begrundelsen står i
  `docs/log/2026-11-04-chromium-fps.md`. (~18:03)
- [udført] Proxy vnext12 `dcc0a68f` installeret, Firefox startet med
  `CYAN_DEPTHCLEAR=1`, BiDi-preload og Poki-spillet åbnet. (~18:05)
- [udført] Spillet bekræftet spilbart med dreng, skinner, tog, Halloween-scene,
  lyd og input. (~18:06)
- [aftalt] Næste FPS-rækkefølge: mål baseline i samme session, test
  `layout.frame_rate=30`, kør kort refresh-driver-timing, test derefter
  canvas-nedskalering. (~18:14)
- [udført] Testkørslen lukket ved sessionsslut. BiDi-sessionen kunne ikke
  lukkes med `browser.close` (`Maximum number of active sessions`), så
  Firefox blev afsluttet med SIGTERM. Ingen profiler-dump var nødvendigt.
  Temperaturen faldt fra 104 °C til 79 °C. (~23:04)
- [udført] `chromium-fps` merget ind i `trunk` som fast-forward
  (`3563028..4454ae1`). Denne handover er opdateret til `trunk`. (~23:10)
- [udført] Dokumentationscommit `065d217` på `trunk` opdaterer handoveren til
  den merged branch. (~23:10)
- [aftalt] Næste FPS-arbejde kører på den nye branch
  `firefox-fps-tuning`, oprettet fra `trunk` ved `065d217`. (~23:10)
- [udført] Den globale git-e-mail er sat til
  `kristian.n.jensen@gmail.com`; global `user.name` er fortsat tom. Tidligere
  commits blev derfor amended/committet med eksplicit
  `Kristian Nygaard Jensen`. (~23:15)
- [udført] Dokumentationsaudit ved sessionsafslutning: branchskiftet,
  bindapi-scriptets flytbare nøglesti og git-identiteten er nu dokumenteret.
  (~23:15)
- [aftalt] Den prioriterede FPS-plan genoptages fra `TODO.md`: ren baseline,
  derefter `layout.frame_rate=30`, refresh-driver-timing og S3. (~23:19)
- [udført] Bokstilstand kontrolleret: `192.168.0.135`, DHCP-ruten og DNS
  virker, uret er korrekt, `pvrsrvkm` er indlæst, X kører på `vt7`, vnext12
  `dcc0a68f` er aktiv, og `/dev/sw_sync` er 0666. Hverken Firefox eller BiDi
  kørte. (~23:19)
- [udført] Første baseline-forsøg startet med vnext12, alpha-shim via BiDi og
  `CYAN_DEPTHCLEAR=1`; WebGL-canvas `1031x580` blev oprettet, og spilprocessen
  fortsatte med draws. Brugeren rapporterede sløring og forsvundet
  spilområde. (~23:21)
- [udført] Den ugyldige kørsel blev stoppet med SIGTERM på kendte PID'er,
  Firefox lukket, temperaturen faldt til 72 °C, og loggerne blev gemt i
  `/root/fps_baseline_invalid_20261104_2322` på boksen. (~23:23)
- [udført] X genstartet med den dokumenterede livline; X gik fra `vt7` til
  `vt8`. Firefox og BiDi blev startet igen med den præcise spillbare opskrift,
  men uden automatiske spilklik. (~23:23)
- [udført] Spillet indlæst korrekt til tutorial-skærmen og bekræftet med
  framebuffer-dump: Halloween-scene, tog, skinner og
  `Press Arrow Key Right` er synlige. (~23:27)
- [afventer] Brugeren skal fysisk trykke pil højre og spille i mindst 40 s;
  derefter måles den rene FPS-baseline i den aktive spillørsel. (~23:28)
- [udført] Brugeren startede gameplay, og den rene baseline blev målt:
  skærmen 1,4 opdateringer/s og spillets rAF 1,3-1,4 fps. (~23:33)
- [aftalt] Fortsæt den prioriterede A/B-test: gem `user.js`, sæt
  `layout.frame_rate=30`, genstart Firefox og mål samme scenarie. (~23:34)
- [udført] `user.js` sikkerhedskopieret til
  `/root/user.js.orig.1793831663`. Diff viser kun den ønskede ændring:
  `user_pref("layout.frame_rate", 30);`. (~23:37)
- [udført] Firefox genstartet med `layout.frame_rate=30`; spillet er igen
  synligt i Halloween-scenen med canvas `1031x580`. Under indlæsningen var
  rAF 1,2-1,3 fps og temperaturen 107 °C. (~23:39)
- [afventer] Brugeren skal spille samme scenarie igen, så frame-rate-varianten
  kan måles mod baseline 1,4/s. (~23:40)
- [udført] `layout.frame_rate=30` målt i aktivt gameplay kl.
  23:42:09-23:42:49: skærmen gav **1,3 opdateringer/s**, spillets rAF
  1,3-1,4 fps og temperaturen 107 °C. Ingen gevinst mod baseline 1,4/s.
  (~23:43)
- [udført] Firefox og BiDi stoppet; den oprindelige `user.js` genskabt
  bit-identisk fra backup (`md5 0ac16be8…`). Temperaturen faldt til 82 °C.
  (~23:43)
- [udført] Første `MOZ_LOG=nsRefreshDriver:5`-forsøg skrev ikke den ønskede
  fil, fordi `MOZ_LOG_FILE=/root/...` blev afvist af brugeren `kristian`
  (samme rettighedsproblem som fælde 43). Logfilen blev flyttet til `/tmp`, og
  Firefox skrev derefter `.moz_log`-filer for parent og child-processer.
  (~23:45)
- [udført] Timingskørsel gennemført med spillet og cirka 676 KB
  refresh-driver-log. Brugeren startede spillet, men spilområdet forsvandt
  igen; den visuelle kørsel er derfor ugyldig, mens loggen kan analyseres.
  Loggen viser både `ticking drivers...`, `Completed transaction id N`,
  `Hit max pending transaction limit, entering wait mode` og
  `Over max pending transaction limit when trying to paint, skipping`.
  Beviset er gemt i `/root/nsrefresh_evidence_20261104_2348` på boksen.
  (~23:48)
- [udført] Den tunge logkørsel stoppet; Firefox/BiDi er lukket, og
  temperaturen faldt fra 107 °C til 74 °C. (~23:48)
- [foreslået] Kør en kort kontrol med `timestamp` som speciel MOZ_LOG-modul;
  den første log har korrekte hændelser, men ingen tidsstempler. (~23:49)
- [udført] `timestamp`-modulet virker. Tidsstemplet log viser cirka 18
  refresh-driver-ticks/s, cirka 17,6 skipped paints/s og kun 1,33-1,35
  completed transactions/s i aktivt gameplay. Median wait-mode er 0,65 s,
  og medianafstanden mellem completions er 0,74 s. (~23:53)
- [udført] Firefox' indholdsproces crashede med `Segmentation fault` efter
  fortsatte WebGL-draws. `dmesg` viser hverken OOM eller en ny PVR/GPU-fault;
  den gamle BIF0-fault er fra cirka fem timer tidligere. Crashloggerne er
  gemt i `/root/firefox_crash_20261104_2355` og
  `/root/nsrefresh_timestamp_evidence_20261104_2352`. (~23:56)
- [udført] Den konstante dunkende/skydende lyd kom fra PulseAudio
  (`pid 8279`), som ikke svarede på `pactl` og fortsatte med den sidste
  buffer. Processen blev stoppet og genstartet som `kristian`; den nye server
  har `auto_null` som sink og **ingen aktive sink-inputs**. (~23:55)
- [udført] Brugeren bekræftede fysisk, at den konstante lyd var stoppet.
  (~23:57)
- [foreslået] Ingen flere spilkørsler før crash-signaturen er undersøgt;
  S3-canvas-halvering er derfor sat på pause. (~23:56)

## Aktuel bokstilstand

- IP: `192.168.0.135` (eth0 har også den statiske fallback `192.168.1.50`).
- Uret er sat fra laptoppen.
- GPU: `pvrsrvkm` indlæst, `gpu_up.sh` kørt med `pvrsrvctl-exit=0`.
- Bindapi-lappen er aktiv; alle fire API'er returnerer 1.
- EGL-proxy: vnext12 `dcc0a68f175b21590eb9efbce4c8ce44`.
- Firefox-profilen er `/home/kristian/ffprof`.
- **Ved handover kører hverken Firefox, BiDi eller Chromium.**

## Målinger i denne session

`/root/fb_fps /dev/fb0 10 20` under den kørende Firefox-session:

```text
fb: 1920x1080 virt=1920x1080 bpp=16 stride=3840 smem_len=16777216 yoffset=0
buffer 0: 47 skift i alt 0 17 14 16 0 0
mest aktive blok skiftede 6 gange -> 0.6 skaerm-opdateringer/s
```

Målingen er ikke en ren baseline: spilleren var allerede i gang/på pause, og
historiske målinger har vist stor variation mellem sessioner. Ved næste
session skal der måles 40 s med frisk spilstart og `POLL=20`.

## Hvorfor Firefox er aktiv vej

Firefox er ikke hurtig, men den virker. De kendte FPS-målinger er ca.
2,4-2,6/s ved 1080p og 4,2-4,8/s i et 640x360-vindue. Loftet ligger i
Firefox' frame-planlægning og canvas/compositor-vejen, ikke i framebufferen,
X-serveren eller CPU-frekvensen.

Chromium er parkeret, fordi det ikke bare mangler ydeevne: dets 3D-verden
renderes forkert, og rendereren crasher med flere uafhængige
hukommelsesfejl. Det er et dybere driver-/ANGLE-/V8-spor end FPS-arbejdet.

## Næste skridt i prioriteret rækkefølge

1. **Ren baseline i en ny session.** Start Firefox på den kendte gode måde,
   spil i mindst 40 s, og mål både `fb_fps` (`POLL=20`) og spillets rAF/`FPS`.
2. **`layout.frame_rate=30`.** Tag backup af
   `/home/kristian/ffprof/user.js`, sæt `user.lockPref`/pref midlertidigt,
   genstart Firefox og mål samme scenarie. Hvis rAF stiger markant, sidder
   loftet i vsync-/tick-planlægningen. Genskab `user.js` bagefter.
3. **Kort `MOZ_LOG=nsRefreshDriver:5`-kørsel.** Mål tiden fra tick til
   "Completed transaction". Brug kun loggen til timing, ikke til FPS.
4. **S3-canvas-nedskalering.** Halvér canvas' backing-størrelse via
   BiDi-preload og behold CSS-størrelsen. Efterprøv inputkoordinater og om
   spillet stadig kan spilles.
5. Hvis 1-4 ikke flytter noget: undersøg S4 (governor/DDR/swap), og gem
   DDK 1.8/4.4-kernesporet som et større projekt.

## Bring-up og start

```bash
bash devuan/find_box.sh
NOW=$(date +%s)
BOXIP=<ip> bash devuan/box.sh "date -s @$NOW"

BOXIP=<ip> bash devuan/box.sh \
  'lsmod | grep -q "^pvrsrvkm" || insmod /root/pvrsrvkm_leddaz.ko; \
   sh /root/gpu_up.sh; chmod 666 /dev/sw_sync 2>/dev/null || true'

BOXIP=<ip> bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
BOXIP=<ip> bash devuan/box.sh \
  'cp -f /root/egl_proxy_dcc0a68f.so.bak /opt/hybris/libEGL.so.1.0.0'

BOXIP=<ip> bash devuan/box.sh \
  'GAME_URL=about:blank CYAN_DEPTHCLEAR=1 \
   nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &'

BOXIP=<ip> bash devuan/box.sh \
  'PYTHONUNBUFFERED=1 nohup python3 /root/bidi_cyan.py preload \
   goto https://poki.com/en/g/subway-surfers 7200 \
   > /root/bidi_cyan.log 2>&1 &'
```

Mål uden at genstarte spillet:

```bash
BOXIP=<ip> bash devuan/box.sh '/root/fb_fps /dev/fb0 40 20'
BOXIP=<ip> bash devuan/box.sh 'cat /sys/rockchip_thermal/temp'
```

## Fælder

- `CYAN_LIGHT=1` må ikke bruges når billedet skal ses; præsentationen går i
  stå.
- `pkill -f` kan dræbe egen ssh-session; dræb på PID eller brug
  `bidi_cyan.py close`/`kill_bidi.sh`.
- BiDi tillader kun én aktiv session; en gammel klient kan blokere `domcheck`
  og `close`. Hvis `browser.close` svarer "Maximum number of active sessions",
  kan Firefox afsluttes med SIGTERM; profiler-dumpet springes så over, men
  det er uden betydning for FPS-testen.
- `MOZ_LOG=nsRefreshDriver:5` er tungt og kan give netværks-/load-timeouts;
  brug det kun til timing.
- X kan skifte VT ved `service nodm restart`; læs den aktive VT.
- Temperaturen var 107 °C under gameplay. Overvåg den og hold pause nok til
  at undgå kernens 120 °C-nedlukning.
- Efter reboot skal bindapi-lappen og vnext12 `dcc0a68f` lægges på igen.
- Global `git config user.name` er tom. Ved commits skal navnet sættes
  eksplicit med `git -c user.name='Kristian Nygaard Jensen' ...`, ellers kan
  Git auto-generere en forkert identitet.
