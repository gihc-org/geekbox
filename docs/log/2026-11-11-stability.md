# Stabilitetstest - session-notat 11. nov 2026

> Agent/model: `codex` / `deepseek-flash`. Branch: `firefox-fps-tuning`.

## Status i ét blik

Stabilitetssporet fortsætter fra 8. nov. Handoverens næste skridt er en ny
længere spilkørsel med den rettede stabilitetsmonitor og IPC-/hang-logging.
`AGENTS.md` er synkroniseret med den nyeste handover, så nye sessioner ikke
starter på den forældede 4. nov-plan.

Spillet kører i 960x540 ved `PRESS TO PLAY`. IPC-logeksperimentet blev
afklaret: Firefox oprettede seks `.moz_log`-filer, men de er alle 0 byte, og
`libxul.so` indeholder ingen selvstændig `IPC`-logmodulstreng. Den videre
test bruger derfor proces-, mapping-, hukommelses- og hang-data.

Brugeren oplevede først, at spilområdet forsvandt ved start, men kunne starte
spillet ved at scrolle op. Et framebuffer-dump kl. 20:08 bekræfter aktiv
gameplay, mens både `Web Content` og Firefox-parenten lever. Framebufferens
mest aktive blok skiftede 3,3 gange/s. Det er derfor indtil videre et separat
UI-/viewport-symptom, ikke det ventede tab-crash.

Crashsporet blev reproduceret kl. 20:24 efter cirka 22 minutter, igen mens
spillet var pauset. `Web Content` forsvandt, mens parenten fortsatte.
`pvr_sync`-advarslen om manglende IPC-descriptors blev fulgt op: parenten
havde **4091 af 4096 file descriptors** åbne, hvoraf **3968 var
`anon_inode:sync_fence`**. Lækkilden er `X11NativeWindow::queueBuffer()` i
`eglplatform_x11.cpp`, som modtog og ignorerede én fence-fd pr. præsenteret
frame. Ved cirka 3 fps giver det cirka 3970 fds på 22 minutter.

Koden lukker nu `fenceFd` i både `queueBuffer()` og `cancelBuffer()`. Et
genopbygget `eglplatform_x11.so` er installeret; den tidligere binære fil er
gemt som `eglplatform_x11.so.pre-fence-fix`. Startscriptet har fået en
sikkerhedsgrænse på 16384 fds, og monitoren logger nu både samlet antal fds
og antal sync-fences per Firefox-proces.

Første kontrolkørsel med fixet startede kl. 20:39 med 146 fds og 4
sync-fences. Efter 60 sekunder var tallene 140 og 3. Før fixet ville den målte
præsentationsrate have tilføjet cirka 180 fence-fds i samme periode. Den
oprindelige lækage er dermed stoppet i den korte kontrol.

Den planlagte timetest kørte til kl. 21:39 med `parent=1`, `content=1` og 4
sync-fences hele vejen. Ved testslut havde parenten 136 fds og 4 fences, mens
`Web Content` havde 51 fds og ingen fences. Dermed holder fixet gennem den
tidligere crashgrænse på 20-22 minutter.

Firefox fortsatte til kl. 00:56, hvor en ny langtidshændelse gav segmentation
fault i en `threaded-ml`-tråd og efterfulgtes af `Exiting due to channel
error`. Det skete cirka 4 timer og 18 minutter efter start, altså uden for
monitorvinduet og langt efter den tidligere FD-grænse. Logs er arkiveret i
`/root/crash_fastplay_20261112_0056/`. Dette er et separat langtidsspor, indtil
årsagen er målt.

## Aftaler og beslutninger

- [aftalt] Fortsæt stabilitetssporet med en ny længere test og relevant
  IPC-/hang-logging. (20:02)
- [aftalt] Undersøg ikke mønt-/bogstavfejlen eller FPS, før stabiliteten er
  afklaret. (20:02)
- [udført] `AGENTS.md` er opdateret til at pege på 8. nov-handoveren og
  `docs/log/2026-11-08-stability.md`. (20:02)
- [udført] Boksen er forhåndstjekket: 192.168.0.135, GPU og vnext12 matcher,
  X kører på `vt8`, lyden er `alsa_output.dmixer`, og der er 6 GB ledigt.
  (20:02)
- [udført] Fast-play er startet i 960x540 med `MOZ_LOG=IPC:3`; screenshot
  viser den korrekte `PRESS TO PLAY`-scene. (20:03)
- [udført] IPC-logmodulet er tavst: seks `.moz_log`-filer blev oprettet, men
  alle er 0 byte. Brug proces-/mappingdata og `dom.ipc.processHangMonitor`
  som næste undersøgelse. (20:04)
- [udført] Stabilitetsmonitoren kører i 40 minutter som PID 6709 med log
  `/tmp/stability_20261111_run1.log`. (20:04)
- [udført] Brugeren kunne starte spillet efter at have scrollet op; et
  framebuffer-dump viser aktiv gameplay, og `Web Content` lever. (20:08)
- [foreslået] Undersøg senere det separate UI-/viewport-symptom, hvor
  spilområdet ser forsvundet ud, indtil der scrolles op. (20:08)
- [udført] Den kendte fejl blev reproduceret kl. 20:24 efter cirka 22
  minutter med pausen aktiv. `Web Content` forsvandt, mens parenten fortsatte.
  (20:24)
- [udført] **Rodårsag fundet:** parenten nåede 4091/4096 file descriptors,
  heraf 3968 `sync_fence`; `queueBuffer()` lukkede ikke den fence-fd, som
  native-window-lageret overdrager for hver præsenteret frame. (20:36)
- [udført] `queueBuffer()` og `cancelBuffer()` lukker nu `fenceFd`, og det
  genopbyggede platformmodul er installeret på boksen. (20:37)
- [udført] Startscriptet hæver FD-grænsen til 16384 som sikkerhedsmargin, og
  monitoren logger `FDs` og `SyncFences`. (20:37)
- [aftalt] En ny længere test skal bevise, at `SyncFences` ikke længere vokser,
  og at spillet kan stå pauset i mindst 25-30 minutter uden tab-crash.
  (20:37)
- [udført] Kontrolkørsel med fixet: 146 fds/4 fences ved start og 140 fds/3
  fences efter 60 sekunder. Der er ingen tegn på den gamle vækst på cirka
  3 fences/s. (20:40)
- [udført] Timetesten kørte til kl. 21:39 med `Web Content` intakt og 4
  sync-fences hele vejen; den tidligere 20-22-minutters crash blev ikke
  reproduceret. (21:39)
- [udført] En senere og separat hændelse indtraf kl. 00:56 efter cirka 4 timer
  18 minutter med segmentation fault i `threaded-ml` og channel error. Logs er
  arkiveret i `/root/crash_fastplay_20261112_0056/`. (00:56)
- [aftalt] Den næste test skal dække langtidsforløbet og følge FDs, mappings,
  trådantal, RSS og ledig hukommelse over mindst fire timer. (14:40)

## Næste skridt

1. Kontrollér boks, GPU, X, oprydningsværktøjer og monitor.
2. Verificér at `MOZ_LOG=IPC:3` faktisk skriver `.moz_log`-filer.
3. Start en ny langtidskørsel og følg FDs, sync-fences, mappings, trådantal og
   hukommelse frem mod og forbi fire timer.
4. Hvis langtidshændelsen gentages, mål om den korrelerer med
   adresseplads/mappings, hukommelsespres eller den separate
   `threaded-ml`-tråd.

## Kommandoer

```bash
# På boksen
bash /root/kill_firefox.sh
RESTART=1 bash /root/play_fast_game.sh
nohup /root/stability_monitor.sh 1800 5 \
  >/tmp/stability_monitor.out 2>&1 &
```
