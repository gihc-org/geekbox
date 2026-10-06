# Chromium-sporet — session-notat 6. okt 2026

> Agent/model: `codex` / `deepseek-v4-flash` (læst fra denne sessions egen
> metadata: `~/.codex/sessions/2026/10/06/rollout-2026-10-06T18-50-48-…jsonl`
> → `"model":"deepseek-v4-flash"`). Branch: `chromium-fps`.
> Fortsætter handover-prompten i `TODO.md` (Chromium-sporet, 2. okt 2026,
> `0338a0f`) og `docs/log/2026-10-01-chromium-fps.md`.

## Status i ét blik (6. okt 2026 ~19:10)

- Boksen blev fundet på **192.168.0.103** (dropbear) og bragt op **uden**
  `bringup_after_power.sh`: ur sat fra laptoppen, `pvrsrvkm_leddaz.ko` +
  `gpu_up.sh` (`pvrsrvctl-exit=0`), bindapi-lap (`eglBindAPI` 1 for alle fire
  API'er), opdateret `start_chromium_probe.sh`. EGL-lappen i `/opt/hybris` var
  intakt (md5 `2d66a43c`).
- **Fejl i 2. okt-rettelsen fundet og rettet:** `start_chromium_probe.sh` havde
  stadig en **hårdkodet `--enable-zero-copy`** i Chromium-argumentlisten ud over
  den nye `CHROMIUM_ZERO_COPY`-variabel. Zero-copy var derfor ALDRIG slået fra i
  sidste sessions "uden flaget"-kørsel — hele fravalgs-målingen 2. okt er
  tvivlsom. Linjen er fjernet (script md5 nu `297429ac`).
- **Ren kørsel med frisk profil endte med "Aw, Snap!".** GPU-processen
  (PID 1969) udløste et **PVR/RGX-firmware-fault** ved første tunge brug:
  `Recovery 1: PID = 1969 … Guilty Lockup` + `BIF0 - FAULT` (MMU status
  `0x2001`, læsning fra `0x01001740C0`, PDS_USC, Banks 4-7). Derefter SIGSEGV i
  renderer-processen (crashpad-dump 18:59:17, exception `0x0b` = SIGSEGV,
  adresse `0x68cc0000`) → fejlside.
- **Anden rene kørsel gav samme fejlside, men en ANDEN crash:** intet nyt
  PVR-fault (dmesg stadig 2 lockups), og spillet nåede længere — det loadede
  rigtige Subway Surfers-assets (`character-ninja-idle`, `character-tasha-idle`,
  `character-king-idle`, `character-brody-idle`, `character-previews`, 40
  assets). Loggen ender med `*** stack smashing detected ***: terminated`;
  crashpad-dump 19:09:01 har exception `0x06` = **SIGABRT** (netop hvad glibc's
  `__stack_chk_fail` rejser) i **samme renderer begge gange**
  (`--renderer-client-id=5`). Dvs. en bufferoverløb-i-renderer, ikke (kun) en
  GPU-kontekstfejl.
- I begge kørsler kører der **annoncer** på Poki-siden (bl.a. `1x1.a-mo.net`,
  `…ay.delivery`), som spawer mange ekstra renderer-processer (målt op til ~15
  samtidige) — en mulig kilde til både støj og ustabilitet.
- Skærmen står **stille** på fejlsiden: identisk fb0-hash to gange, 99,3 % hvid
  med en lille mørk figur i midten (fejlsidens ikon), ikke et spil.
- **Dette er samme fejlklasse som Firefox-sporet ramte 26. aug:**
  `BIF0 - FAULT` → lockup-recovery → ødelagt kontekst → SIGSEGV i
  renderer-tråden (se `docs/log/2026-08-26-gpu-fault-gennembrud.md`). Dengang
  forsvandt faulten med buffer-lifecycle-fixet i `eglplatform_x11`. Hypotesen er
  nu, at "frosset på logo" 2. okt OGSÅ var dette fault — ikke zero-copy.

## Aftaler og beslutninger

- [udført] Boksen bragt op uden `bringup_after_power.sh` (ur, `pvrsrvkm`,
  `gpu_up.sh`, bindapi-lap, start-script). (~18:56)
- [udført] Hårdkodet `--enable-zero-copy` fjernet fra
  `start_chromium_probe.sh` — fejl i 2. okt-commit `0338a0f`. (~18:57)
- [udført] Ren Chromium-kørsel med frisk profil (gammel profil flyttet til
  `chromium-fps.bak-…`); endte i renderer-crash efter PVR-GPU-fault. (~18:59)
- [udført] Anden ren kørsel: ingen ny GPU-fault, men rendereren døde med
  `stack smashing detected` (SIGABRT) under spillets asset-load. (~19:09)
- [udført] Tredje kørsel med annoncedomæner blokeret i Chromium
  (`--host-resolver-rules`): ingen GPU-fault, men rendereren crashede igen
  (SIGSEGV i heap-området, uden for alle indlæste moduler) — annoncerne er
  altså ikke årsagen. (~20:55)
- [aftalt] Brugeren har givet agenten fuld kommando-adgang uden
  godkendelsesprompter. Aftalen står nu i `AGENTS.md` under "Samarbejdet med
  brugeren": agenten spørger KUN om fysiske handlinger (tænde/slukke, kabler,
  klik/tast, se på skærmen). `~/.codex/config.toml` har fået
  `approval_policy = "never"` + `sandbox_mode = "danger-full-access"`. (~21:05)
- [afventer] Beslutning med brugeren: skal vi (a) køre med annonceblokering
  (CDP'ens `game`-kommando) for at fjerne annonce-variablen, (b) rulle
  zero-copy tilbage til 2. okt-tilstanden for at se om crashet er nyt, eller
  (c) gå efter en rigtig backtrace af renderer-crashet (gdb/core-dump)?

## Hvad vi ved (målt 6. okt)

- Chromium 150.0.7871.181, `--use-gl=angle --use-angle=gles-egl`,
  software-compositing, `--disable-gpu-watchdog`, `CYAN_DEPTHCLEAR=1`,
  zero-copy FRA (efter rettelsen), `EGL_PROXY_CONFIG_FIX=1`,
  `EGL_PROXY_FAKE_VISUAL=0x21`.
- Spillet **loadede** fint (log: `[ResourcePlugin] Loading complete`,
  `[ResourceBridge] Converting PK data to Three.js geometries`), og spillets
  konsol kørte videre efter faulten.
- PVR-faulten ramte **18:57:56** (dmesg 3336 s efter boot), få sekunder efter at
  GPU-processen startede. Kun **2** `Guilty Lockup` og **2** `BIF0 - FAULT` i
  dmesg (samme to dump, ikke et gentaget mønster).
- Intet OOM: 762 MB brugt af 1998 MB, swap urørt. Ingen segfault-linjer i dmesg
  (Chromium fanger selv signalet via crashpad).
- Begge crash-dumps er fra en renderer med `--renderer-client-id=5`; den ene
  med SIGSEGV (`0x68cc0000`) efter GPU-faulten, den anden med SIGABRT
  (stack-smashing) uden nogen GPU-fault i dmesg.
- Efter crashet svarer CDP stadig på `/json/list` (titel/URL = Poki-siden), men
  `Runtime.evaluate` og `Page.createIsolatedWorld` timeout'er — renderer-tråden
  hænger/sad-tab.
- **Observation der skal tjekkes:** fb0-dumpet er nu 2.073.600 bytes
  (1920×540×16) og `dd` rammer EOF der, mens `fbset` melder 1920×1080×16. I
  tidligere sessioner blev der konverteret ~4 MB-dumps med
  `rgb565_to_png.py`. Om det er et nyt mode-/timing-fænomen (jf.
  ugens-logo/timing-fælden i `docs/boksen/skaerm.md`) eller et artefakt skal
  afklares.

## Næste skridt

1. Fjern annonce-variablen: kør spillet via CDP'ens `game`-kommando (blokerer
   annonce-/trackerdomæner) og se om renderer-crashet forsvinder eller flytter
   sig. Billigt og i tråd med TODO-punktet om en MV3-adblocker.
2. Få en rigtig backtrace af `stack smashing`-crashet (gdb på rendereren via
   `--renderer-cmd-prefix`, eller core-dump med crashpad slået fra) — dét giver
   rodårsagen i stedet for endnu en symptommåling.
3. Spørgsmålet om zero-copy (2. okt-rettelsen) er stadig uafklaret, men bør
   først besvares når rendereren ikke længere dør: kør én gang med
   `CHROMIUM_ZERO_COPY=1` og sammenlign canvas-hashen på fb0.
4. Først derefter giver fps-målinger mening.

## Kommandoer der virker (6. okt)

Bring-up (uden `bringup_after_power.sh`):

```bash
bash devuan/find_box.sh
NOW=$(date +%s); BOXIP=<ip> bash devuan/box.sh "date -s @$NOW"
BOXIP=<ip> bash devuan/box.sh 'lsmod | grep -q "^pvrsrvkm" || insmod /root/pvrsrvkm_leddaz.ko; sh /root/gpu_up.sh; chmod 666 /dev/sw_sync'
BOXIP=<ip> bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
BOXIP=<ip> bash devuan/box.sh --put devuan/gpu/eglplatform_x11/start_chromium_probe.sh /root/start_chromium_probe.sh
```

Ren kørsel + CDP:

```bash
BOXIP=<ip> bash devuan/box.sh 'CHROMIUM_LD_PRELOAD= CHROMIUM_USE_GL=angle CHROMIUM_USE_ANGLE=gles-egl \
  CHROMIUM_URL=https://poki.com/en/g/subway-surfers nohup /root/start_chromium_probe.sh > /root/chromium_launch.log 2>&1 &'
ssh -f -N -i ~/.ssh/geekbox_key -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -L 9223:127.0.0.1:9223 root@<ip>
python3 devuan/gpu/eglplatform_x11/chromium_cdp.py probe 8
```

GPU-fault-jagt:

```bash
BOXIP=<ip> bash devuan/box.sh 'dmesg | grep -c "Guilty Lockup"; dmesg | grep -n "BIF0 - FAULT" ; dmesg | tail -40'
BOXIP=<ip> bash devuan/box.sh 'ls -la --time-style=+%H:%M:%S "/home/kristian/.config/chromium/Crash Reports/pending/" | tail -6'
```

## Fælder (nye, 6. okt)

- `pkill -f "<mønster>"` dræber også ens egen ssh-session, når mønsteret står i
  den kommando man selv kører (samme som fælde 41). Brug `ss -ltnp`/`pgrep -af`
  og dræb på PID.
- Crashpad-databasen er `/home/kristian/.config/chromium/Crash Reports` (ikke
  profil-mappen `chromium-fps`), så gamle dumps fra 30. sep ligger blandet med
  dagens — filtér på tidsstempel.
- `python3 chromium_cdp.py` skal køres uden for sandkassen (socket-adgang);
  `ssh`/`scp` til boksen ligeså.
