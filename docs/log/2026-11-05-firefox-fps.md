# Firefox/FPS - session-notat 5. nov 2026

> Agent/model: `codex` / `deepseek-flash` (fra denne sessions egen
> `turn_context`). Fortsætter `docs/log/2026-11-04-firefox-fps.md` efter
> midnat. Branch: `firefox-fps-tuning`.

## Status i ét blik

- Ren gameplay-baseline: **1,4 skærm-opdateringer/s** og **1,3-1,4 rAF/s**.
- `layout.frame_rate=30` gav **1,3/s** og flyttede derfor ikke loftet.
- Tidsstemplet `nsRefreshDriver`-log viser cirka **18 ticks/s**, men næsten
  alle går i wait-mode: kun **1,33-1,35 completed transactions/s**. Median
  wait-mode er 0,65 s, og medianafstanden mellem completions er 0,74 s.
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
  og temperaturen er faldet til 74 °C.

## Aftaler og beslutninger

- [udført] Ren baseline målt i aktivt gameplay med `POLL=20`. (~23:33)
- [udført] `layout.frame_rate=30` testet og `user.js` genskabt bit-identisk.
  (~23:43)
- [udført] `MOZ_LOG=timestamp,nsRefreshDriver:5` viste tick- og
  transaction-mekanismen. `timestamp` er nødvendigt; logfilen skal ligge i
  `/tmp` eller under `/home/kristian`, fordi Firefox ikke kan skrive i `/root`.
  (~23:53)
- [udført] FireFox/BiDi lukkes fremover via `CLOSE_WHEN_DONE=1`, som kalder
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
- [udført] `layout.throttle_in_process_iframes=false` testet isoleret med
  backup og diff. Resultatet var 1,3 skærm-opdateringer/s og 1,3-1,4 rAF/s,
  altså ingen ændring. `user.js` er genskabt bit-identisk
  (`md5 0ac16be8…`). (~00:33)
- [udført] Testkørslen lukket pænt via BiDi. Firefox, BiDi og lydstreams er
  stoppet; temperaturen er faldet til 74 °C. (~00:33)

## Vigtige kommandoer

```bash
# Firefox
GAME_URL=about:blank CYAN_DEPTHCLEAR=1 \
  nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &

# BiDi med S3 og pæn lukning (send senere SIGTERM til kun BiDi-PID'en)
CYAN_CANVAS_SCALE=0.5 CLOSE_WHEN_DONE=1 PYTHONUNBUFFERED=1 \
  nohup python3 /root/bidi_cyan.py preload goto \
  https://poki.com/en/g/subway-surfers 600 > /root/bidi_cyan.log 2>&1 &

# Måling
/root/fb_fps /dev/fb0 40 20
```

## Beviser på boksen

- `/root/nsrefresh_timestamp_evidence_20261104_2352`: tidsstemplede
  refresh-driver-logs.
- `/root/firefox_crash_20261104_2355`: Firefox-, BiDi- og dmesg-log fra
  oprydningen.
- `/root/bidi_cyan.py.pre-s3.20261104`: backup før S3-ændringen.
