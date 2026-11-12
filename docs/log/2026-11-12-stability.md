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

## Næste skridt

1. Vælg næste større spor eksplicit: UI-/viewport-fejlen, dybere FPS eller
   brugervenlighed ved bring-up.
2. Hvis langtidscrash kl. 00:56 gentages, undersøg `threaded-ml` og
   ressourceforløbet frem mod hændelsen.

## Kommandoer

```bash
# På boksen
RESTART=1 bash /root/play_fast_game.sh
nohup /root/stability_monitor.sh 18000 5 \
  >/tmp/stability_monitor_20261112.out 2>&1 &
```
