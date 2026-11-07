# Firefox fast-play - session-notat 7. nov 2026

> Agent/model: `codex` / `gpt-5`.

## Status i ét blik

Fast-play-launcheren `devuan/gpu/eglplatform_x11/play_fast_game.sh` er valgt
som næste praktiske skridt efter FPS-målingerne 5. nov. Standardvinduet er
960x540, fordi det gav 3,2 skærmopdateringer/s og 37-44 rAF/10 s med en
2,25 gange større flade end 640x360. 640x360 er fortsat valget til maksimal
hastighed. Launcheren skal efterlade Firefox kørende og må ikke ændre
`user.js` eller være et målescript.

Boksen var ved start på `192.168.0.135`, X kørte på `vt8`, GPU-modulet var
indlæst, `/dev/sw_sync` var 0666, og vnext12-proxyen matchede
`dcc0a68f175b21590eb9efbce4c8ce44`. Firefox og BiDi kørte ikke.

Launcheren er implementeret og har bestået en teknisk smoke-test ved
960x540. Den startede Firefox, satte den faktiske renderflade til 960x540,
indlæste Subway Surfers via `preloadmin`, bekræftede en gyldig WebGL-kontekst
og gemte et screenshot, der viser drengen, toget, skinnerne og
`PRESS TO PLAY`. `user.js` var fortsat bit-identisk med
`md5 0ac16be87cdf95b1250043f4efc00816`, og Firefox blev efterladt kørende.

Efter den første content-crash bestod en ny fem-minutters spiltest uden crash.
Manglende lyd blev derefter rettet: PulseAudio var autospawnet uden
legacy-ALSA og brugte `auto_null`. Brugeren bekræftede til sidst, at både
lyden og den valgte 960x540-størrelse virker.

## Aftaler og beslutninger

- [aftalt] Fast-play-launcheren laves før nye FPS-eksperimenter. (22:45)
- [aftalt] 960x540 er standard; `WIN="640 360"` tilbydes som hurtig variant.
  (22:45)
- [aftalt] Launcheren skal patche `xulstore.json` før Firefox-start, indlæse
  spillet via BiDi-preload og efterlade Firefox kørende. (22:45)
- [aftalt] `user.js` må ikke ændres, og løsningen må ikke bruge ny
  frame-rate-pref, canvas-skalering, SWGL eller hardware-WebRender. (22:45)
- [aftalt] Brugeren skal fysisk spille på boksen, før 960x540 vælges som
  endelig standard. (22:45)
- [udført] De krævede handover-kilder er læst: 5. nov-notatet, FPS-analysen
  §0, fælde 41-66 og `OVERBLIK.md`. (22:44)
- [udført] Boksen er forhåndstjekket: GPU, sw_sync og vnext12 er klar, og der
  kører hverken Firefox eller BiDi. (22:45)
- [udført] `play_fast_game.sh` er implementeret med 960x540 som standard og
  `WIN="640 360"` som valgfri hurtig variant. Den patcher kun
  `xulstore.json`, rydder de samme flygtige profildata som den kendte
  målekørsel og ændrer ikke `user.js`. (22:47)
- [udført] Første smoke-test fandt to testfejl: `xwininfo | grep -q` blev
  falsk på grund af `pipefail`, og Openbox' 970x550-ramme blev forvekslet med
  den faktiske 960x540-renderflade. Begge er rettet og dokumenteret som
  fælde 67. (22:55)
- [udført] Endelig 960x540-smoke-test bestod. Firefox kører, WebGL blev
  bekræftet, og screenshotet
  `/tmp/play_fast_game.pQTS6z/shots/shot_65.png` viser spillets
  `PRESS TO PLAY`-scene. (23:00)
- [udført] BiDi-klienten har afsluttet sin session; Firefox kører fortsat ved
  960x540. (23:04)
- [udført] Første fysiske spiltest startede, og spilområdet forsvandt kort,
  men kom tilbage af sig selv. Loggen viser ingen `CTXLOST`, WebGL-fejl eller
  GPU-reset. (23:04)
- [udført] Web Content PID 12726 crashede omkring 23:05:22 med
  `SIGSEGV / SEGV_MAPERR` på adresse `0x6` i `libxul.so`; Firefox-parenten
  og X fortsatte. Minidumpet er arkiveret i
  `/root/crash_fastplay_20261107_2305/`. (23:06)
- [udført] `elfutils` er installeret på boksen til crashanalyse. Den nærmeste
  offentlige `firefox-esr-dbgsym` er fra deb12-builden og har forskellig
  maskinkode, så dens symboler kan ikke bruges sikkert på deb13-builden.
  (23:10)
- [udført] Spillet er genstartet rent efter crashet i 960x540. Ny kørsel står
  klar ved `PRESS TO PLAY`; logs ligger i `/tmp/play_fast_game.YACLr1/`.
  (23:11)
- [udført] BiDi-klienten blev frigjort efter load, fordi den ikke behøves
  under gameplay og kan hænge på den ene session. Launcheren lukker den nu
  selv efter en vellykket indlæsning. (23:12)
- [udført] Brugeren rapporterede, at lyden i den foregående session var
  begyndt at hakke, hvorefter PulseAudio blev stoppet. Den manuelle
  genstart efterlod en server uden legacy-ALSA. (23:33)
- [udført] Manglende lyd blev diagnosticeret: PulseAudio kørte som
  `kristian`, men var autospawnet uden `/opt/alsa-da` og brugte
  `auto_null`. En kontrolleret genstart med legacy-ALSA gav
  `alsa_output.dmixer`; en testtone satte sinken i RUNNING med et aktivt
  sink-input. (23:40)
- [udført] `ensure_audio.sh` er tilføjet, og `play_fast_game.sh` kalder den
  før Firefox-start. Den reparerer kun lydstakken, hvis den ikke allerede
  bruger `alsa_output.dmixer` og legacy-ALSA. (23:42)
- [udført] Brugeren spillede cirka fem minutter i 960x540 uden crash, og
  spilområdet forblev synligt. Stabilitetsaccepttesten bestod. (23:33)
- [udført] Spillet er genstartet med den automatiske lydkontrol. Ny kørsel
  står klar ved `PRESS TO PLAY`; logs ligger i
  `/tmp/play_fast_game.qcBVid/`. (23:43)
- [udført] Brugeren bekræftede, at lyden er tilbage gennem fjernsynet.
  Dermed bestod både fem-minutters stabilitetstesten, 960x540-accepttesten
  og lydtesten. (23:48)
- [aftalt] Dokumentationen skal auditeres mod hele sessionen og derefter
  committes. (23:50)
- [udført] Dokumentationsauditen fandt og rettede de sidste formelle mangler:
  overblikkets indgang pegede på 5. nov, TODO-overskriften havde gammelt
  tidspunkt, og notatet manglede lydhistorik og verifikationskommandoer.
  Relevante 5. nov-værktøjer tages med i commiten; testartefakter som
  `.claude/`, `dmesg-mmc/`, `fb0.png` og `fb0.raw.gz` udelades. (23:51)

## Næste skridt

Fast-play- og lydarbejdet er afsluttet. 960x540 er standard, og
`ensure_audio.sh` kører automatisk fra launcheren. Næste større spor kræver
en ny, udtrykkelig beslutning.

## Åbent crashspor

Crashsignaturen er `SIGSEGV / SEGV_MAPERR` på adresse `0x6` i `libxul.so`,
`libxul+0x32f45f4`. Der var ingen `CTXLOST`, GPU-fault eller DeviceReset.
Minidump og `.extra` ligger i `/root/crash_fastplay_20261107_2305/`.
Den eksakte `firefox-esr-dbgsym_140.12.0esr-1~deb13u1_armhf.deb` er ikke
fundet i Debians offentlige debugarkiv. En efterfølgende fem-minutters
spiltest crashede ikke. Hvis crashet senere gentages, er minidumpet beviset
der skal analyseres videre.

## Kommandoer

```bash
# På boksen
bash /root/play_fast_game.sh
WIN="640 360" bash /root/play_fast_game.sh
RESTART=1 bash /root/play_fast_game.sh

# Lydkontrol
bash /root/ensure_audio.sh
runuser -u kristian -- env XDG_RUNTIME_DIR=/run/user/1000 pactl info
runuser -u kristian -- env XDG_RUNTIME_DIR=/run/user/1000 \
  pactl list short sinks
runuser -u kristian -- env XDG_RUNTIME_DIR=/run/user/1000 \
  speaker-test -D pulse -c 2 -t sine -f 440 -l 1
```
