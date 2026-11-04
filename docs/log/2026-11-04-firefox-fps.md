# Firefox/FPS-sporet - session-notat 4. nov 2026

> Agent/model: `codex` / `deepseek-flash` (læst fra denne sessions
> `turn_context` i `~/.codex/sessions/2026/11/04/`). Branch:
> `firefox-fps-tuning`, oprettet fra `trunk` (`chromium-fps` blev merget ind
> 4. nov 2026).
> Fortsætter den spillbare Firefox-vej fra 19.-26. sep 2026.

## Status i ét blik

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
