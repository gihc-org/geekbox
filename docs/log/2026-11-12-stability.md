# Stabilitet og langtidstest - session-notat 12. nov 2026

> Agent/model: `codex` / `deepseek-flash`. Branch: `firefox-fps-tuning`.

## Status i ét blik

Fence-fd-lækagen fra 11. nov er rettet og valideret gennem en fem timer lang
monitorkørsel. `Web Content` levede hele kørslen, antallet af sync-fences
forblev 3-4, og der kom ingen `EVENT`. Ved testslut kl. 19:39 havde parenten
132 fds, 1926 mappings og 316 MB RSS; content-processen havde 50 fds, 1844
mappings og 287 MB RSS. Ressourcetallene var stabile eller faldende.

Firefox kørte fortsat kl. 20:34, altså næsten seks timer efter start, og
spillet var fortsat aktivt. Den tidligere langtidshændelse kl. 00:56 efter
cirka fire timer blev ikke reproduceret. Den skal derfor betragtes som en
enkeltstående hændelse, indtil den eventuelt gentages.

Næste spor er besluttet: UI-/viewport-fejlen undersøges først, derefter laves
et samlet bring-up-flow, og FPS-profilering kommer som tredje spor. UI-sporet
er aktivt nu.

## Aftaler og beslutninger

- [udført] Status blev rekonstrueret fra
  `docs/log/2026-11-11-stability.md`, testloggen og boksens processer.
  (14:37)
- [udført] Den overnattede test og senhændelsen er arkiveret i
  `/root/crash_fastplay_20261112_0056/`. (14:38)
- [aftalt] Den næste kørsel skal være en langtidstest på mindst fire timer med
  måling af FDs, sync-fences, mappings, trådantal, RSS og ledig hukommelse.
  (14:40)
- [foreslået] Undersøg senere det separate UI-/viewport-symptom, hvor
  spilområdet ser forsvundet ud, indtil der scrolles op. (14:40)
- [udført] Femtimersmonitoren kørte fra 14:38 til 19:39 uden tab-crash og
  uden `EVENT`; sync-fences forblev 3-4. (19:39)
- [udført] Brugeren spillede med flere pauser, og Firefox kørte fortsat kl.
  20:34 efter næsten seks timer. (20:34)
- [udført] Logs og screenshot er arkiveret i
  `/root/stability_success_20261112/`. (20:35)
- [aftalt] Stabilitetssporet betragtes som bestået for det praktiske
  spilflow; enkeltcrash kl. 00:56 følges kun op, hvis den gentages. (20:35)
- [aftalt] Rækkefølgen fremad er UI-/viewport, derefter bring-up-flow og
  derefter FPS-profilering. (20:41)
- [aftalt] UI-/viewport-sporet er det aktive spor. (20:41)
- [udført] UI-telemetri er tilføjet til BiDi-preloadet og installeret på
  boksen; den logger viewport, scroll, dokumentstørrelse, canvas-rektangel og
  det øverste element ved canvas' midte, når tilstanden ændrer sig. (20:43)
- [udført] Første telemetri ved `PRESS TO PLAY`: Firefox-renderfladen er
  960x540, men websidens viewport er kun 960x377. Poki-dokumentet er
  960x4992 ved `scrollY=0`; spil-iframen er 640x360, og canvasset fylder
  hele dens frame. (20:44)
- [afventer] Brugeren skal genskabe den synlige fejl og derefter scrolle op,
  så telemetrien kan sammenligne de to tilstande. (20:44)
- [udført] Fejlen blev genskabt: mellemrum med fokus uden for spillet flyttede
  topsiden mellem `scrollY=0`, hvor spil-iframen er synlig, og `scrollY=416`,
  hvor spillet er uden for viewporten. Museklik inde i spil-iframen startede
  spillet korrekt. (20:47)
- [udført] En A/B-beskyttelse er tilføjet: spil-iframen fokuseres automatisk,
  og mellemrum/piletaster/PgUp/PgDn må ikke længere scrolle topsiden, når
  fokus ikke står i et inputfelt. (20:48)
- [udført] Første A/B-start fandt en JavaScript-syntaksfejl i guard-blokken.
  Både `preloadmin` og den fulde preload består nu en Node-syntakskontrol.
  (20:54)
- [udført] Genstart med rettet preload virker: spil-iframen blev fokuseret
  automatisk, og `scrollY` forblev 0. Et consent-overlay stjal senere fokus,
  men scroll-guarden er fortsat aktiv. (20:56)
- [udført] En hakkende lyd efter den hårde genstart blev diagnosticeret som
  den kendte PulseAudio-fejl. `ensure_audio.sh` stoppede serveren og startede
  den igen med `alsa_output.dmixer`; lydstakken er IDLE uden sink-inputs.
  (20:55)
- [udført] Brugeren trykkede mellemrum med musen uden for spilområdet.
  Loggen viser `UISCROLL-GUARD Space active=BODY#.desktopVersion`, og
  spilområdet blev på skærmen. `scrollY` forblev 0. (21:00)
- [udført] Piletaster efter cookie-accept påvirkede ikke spillet eller
  topsidens scroll. Telemetrien viser, at cookie-overlayet havde flyttet
  fokus tilbage til `BODY`; den tidligere fokusgenopretning stoppede efter
  8 sekunder. (21:03)
- [udført] Fokusgenopretning kører nu hvert sekund og springer cookie-overlayet
  over, indtil det forsvinder; derefter får spil-iframen fokus igen. (21:03)
- [udført] Efter cookie-accept blev spil-iframen igen `activeElement`.
  Brugeren trykkede mellemrum, venstre- og op-pil; spillet startede, og
  spilområdet blev på plads. `scrollY` forblev 0. (21:06)
- [udført] UI-/viewport-sporet er bestået. Den præcise tast kan ikke udledes
  af telemetrien, fordi tastetrykkene nu blev korrekt afleveret til spillets
  iframe i stedet for til topsiden. (21:06)
- [aftalt] Bring-up-flowet er det næste aktive spor. (21:06)

## Næste skridt

1. Lav et samlet bring-up-flow, så spillet kan startes lokalt efter en
   strømcyklus uden manuelle SSH-trin.
2. Gå derefter videre til FPS-profilering.
3. Hvis langtidscrash kl. 00:56 gentages, undersøg `threaded-ml` og
   ressourceforløbet frem mod hændelsen.

## Kommandoer

```bash
# På boksen
RESTART=1 bash /root/play_fast_game.sh
nohup /root/stability_monitor.sh 18000 5 \
  >/tmp/stability_monitor_20261112.out 2>&1 &
```
