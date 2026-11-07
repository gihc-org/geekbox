# Stabilitet og næste spor - session-notat 8. nov 2026

> Agent/model: `codex` / `gpt-5`.

## Status i ét blik

Stabilitetssporet er det aktive arbejde. Den første fem-minutters test bestod,
men en længere kørsel endte efter cirka 20 minutter med et nyt tab-crash,
mens spillet var pauset. Firefox-parenten levede fortsat, men dens eneste
tilbageværende tråd stod i `poll_schedule_timeout` og blev rapporteret til
cirka 100 % CPU. Content-processen var væk, og der var endnu ikke skrevet et
nyt minidump.

En anden test gentog crashet efter cirka 22 minutter og den anden pause i
kørselen; den første pause efter cirka 10 minutter crashede ikke. Også denne
gang forsvandt `Web Content`, mens Firefox-parenten fortsatte. Loggen viser
IPC-kanalbrud og `CompositorBridgeChild receives IPC close with
reason=AbnormalShutdown`; der kom ikke et nyt minidump.

En `WebExtensions`-proces fra en tidligere Firefox-kørsel levede samtidig som
orphan med cirka 63 % CPU. Det viser, at den gamle oprydning med
`pkill -x firefox-esr` ikke stoppede alle Firefox-helperprocesser.

## Aftaler og beslutninger

- [aftalt] Stabilitetstestene køres før nye FPS-eksperimenter. (00:03)
- [aftalt] De øvrige kandidatspor skal være eksplicit dokumenteret, så de
  ikke går tabt under crasharbejdet. (00:03)
- [udført] Spillet blev sat på pause efter længere tids gameplay og crashede
  derefter. Den nye kørsel skal behandles som et reproduceret
  stabilitetsproblem, indtil årsagen er kendt. (00:02)
- [udført] I den første observerede hængning var Firefox-parenten beskadiget:
  content-processen var væk, parenten havde kun hovedtråden tilbage og stod
  i en PollWrapper-/poll-løkke med høj CPU. (00:04)
- [udført] En `WebExtensions`-proces fra PID 12566 var efterladt som orphan
  med parent PID 1 og cirka 63 % CPU. (00:04)
- [udført] `kill_firefox.sh` er tilføjet til at stoppe parent, Web Content,
  WebExtensions, Utility Process, Socket Process og crashhelper via præcis
  argv[0]-matchning. `play_fast_game.sh` og `start_cyan_probe.sh` bruger den.
  (00:06)
- [udført] Oprydningen er testet på boksen: den stoppede syv processer,
  inklusive den gamle WebExtensions-helper. Den gamle helper overlevede SIGTERM
  og blev først fjernet med SIGKILL. (00:08)
- [udført] En ny grafisk fejl er observeret: mønter og bogstaver vises det
  meste af tiden som gullige, halvgennemsigtige "skyer" i stedet for skarpe
  objekter. Fejlen er endnu ikke undersøgt. (00:03)
- [udført] `stability_monitor.sh` er tilføjet og smoke-testet. Den logger
  proces-træ, hukommelse, termiske zoner, crashfiler og et eksplicit EVENT,
  hvis Firefox-parenten eller en aktiv content-process forsvinder. (00:11)
- [udført] En 30-minutters monitor kører som
  `/tmp/stability_20261108_001148.log`. Spillet er genstartet med den nye
  oprydning i `/tmp/play_fast_game.QCsAnX/`; 960x540, WebGL og lyd er
  verificeret. (00:12)
- [udført] Den anden monitorede kørsel crashede efter cirka 22 minutter,
  igen under pause nummer to. `Web Content` forsvandt, mens parenten fortsat
  havde alle sine tråde og blev siddende i poll. Loggen viser
  `CompositorBridgeChild receives IPC close with reason=AbnormalShutdown`.
  Der var ingen ny minidump, GPU-fault eller OOM-linje. (00:34)
- [udført] Den første monitorversion talte både Web Content og WebExtensions
  som content-processer. Den blev derfor blind for, at spillets content-proces
  forsvandt. Tællingen er rettet til at udelukke WebExtensions. (00:37)
- [udført] Monitoren logger nu også `VmSize`, `VmRSS`, trådantal og antal
  memory-mappings pr. Firefox-proces. Det er relevant, fordi både Firefox og
  spillet er 32-bit og kan ramme adresseplads-/mapping-grænser efter en lang
  kørsel. (00:39)
- [udført] Game-log, monitorlog, procesliste, dmesg og gdb-stack fra parenten
  er arkiveret i `/root/crash_fastplay_20261108_0035/`. Parentens main-tråd
  stod i `poll()`; Renderer-, Compositor- og WebRender-trådene eksisterede
  stadig, men kunne ikke identificeres nærmere uden matchende symbols. (00:37)
- [udført] Hukommelsesforløbet er analyseret: content-processens RSS steg fra
  cirka 253 MB ved stabil load til cirka 328 MB før crashet. Ledig hukommelse
  faldt fra cirka 279 MB til 134 MB. Der var ingen OOM-linje i dmesg, og
  processen forsvandt uden minidump. (00:40)
- [udført] Sessionen afsluttes rent: monitor og alle Firefox-processer er
  stoppet, og PulseAudio står fortsat på `alsa_output.dmixer`. (00:42)

## Kandidatspor

- [aftalt] **Stabilitet:** find årsagen til tab-crashet, og kør derefter en
  længere test uden crash.
- [foreslået] **Brugervenlighed:** lav et samlet bring-up- og desktopflow, så
  spillet kan startes efter strømcyklus uden manuelle SSH-trin.
- [foreslået] **FPS:** gå videre med dybere Firefox-/WebRender- eller
  DDK/kernel-arbejde. De billige prefs, canvas-skalering, SWGL og
  hardware-WebRender er allerede målt som blindgyder.
- [foreslået] **Accepter nuværende baseline:** 960x540, cirka 3-4 fps og
  fungerende lyd kan bruges som slutmål, hvis stabiliteten bliver acceptabel.
- [afventer] **Mønt-/bogstavrendering:** karakterisér om fejlen er
  gennemsigtighed, teksturformat, shader-state eller manglende sprites.

## Aktuel crashhypotese

Crashmønstret peger på, at Firefox-contentprocessen afsluttes eller dræbes fra
IPC-/compositorvejen, hvorefter parenten fortsætter uden et crash-dump.
Fejlen sker tilsyneladende sent i en længere session og er indtil videre set
efter anden pause, ikke efter den første. Det er ikke bekræftet om
`ProcessHangMonitor`, manglende IPC-descriptors eller en intern
`AbnormalShutdown` udløser forløbet.

## Næste skridt

1. Genstart spillet med den nye oprydning og verificér, at alle processer
   forsvinder ved lukning.
2. Kør en ny længere stabilitetstest med den rettede monitor og relevant
   IPC-/hang-logging.
3. Behandl mønt-/bogstavfejlen som et separat renderingsspor efter
   stabilitetstesten.

## Kommandoer

```bash
# Alle Firefox-processer, også content-helpers
bash /root/kill_firefox.sh

# Spilstart med korrekt oprydning og lydkontrol
RESTART=1 bash /root/play_fast_game.sh

# Stabilitetsmonitor i 30 minutter
nohup /root/stability_monitor.sh 1800 5 \
  >/tmp/stability_monitor.out 2>&1 &

# Verificér at ingen Firefox-processer er tilbage
ps -eo pid,ppid,stat,comm,args | \
  grep -E 'firefox-esr|Web Content|WebExtensions|Utility Process'
```
