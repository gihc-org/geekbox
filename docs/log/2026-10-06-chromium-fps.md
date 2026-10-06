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
- **RODÅRSAGEN FUNDET (6. okt ~21:55): det er V8, Chromiums JS-motor.**
  Crashpad-minidumperne blev hentet ned og adresserne oversat med Debians
  debuginfod (build-id `f0e2662b…` → 197 MB debugfil, `addr2line`). Alle
  crash-dumps er fra en **renderer** (én gang også browser-processen), og de
  rammer V8's hukommelseshåndtering:

| nr | proces | signal | symbol |
|----|--------|--------|--------|
| 1 | renderer | SIGSEGV | `v8::internal::ConcurrentMarking::RunMajor` / `MarkingVisitorBase::ProcessStrongHeapObject` (`v8/src/heap/concurrent-marking.cc`) |
| 2 | renderer | SIGABRT | glibc `abort()` efter `*** stack smashing detected ***` i en `ThreadPoolForegroundWorker`-tråd |
| 3 | renderer | SIGSEGV | samme concurrent-marking-funktion som nr. 1 |
| 4 | browser | SIGSEGV | chromium +0x63b5872 (ikke symboliseret) |
| 5 | renderer | SIGABRT | stack-smashing igen (worker-tråd) |
| 6 | renderer | SIGSEGV | `v8::internal::maglev::MaglevCodeGenerator::RecordInlinedFunctions` (JIT-kompiler, NULL-deref) |
| 7 | renderer | SIGSEGV | `v8::internal::UnifiedHeapMarkingVisitorBase::Visit` (GC-markering) |

   To af fejladresserne lå i heap-området uden for alle indlæste moduler
   (0x64df0334, 0x68cc0000) — altså vilde pointere, ikke dårlig kode-adresse.
- **Isolationsforsøg der alle crashede igen (1-2 min inde i spillet):**
  annoncedomæner blokeret i Chromium (`--host-resolver-rules`),
  `--js-flags=--no-concurrent-marking --no-parallel-marking`,
  `--js-flags=--single-threaded` og `--js-flags=--jitless`. GPU-faulten i
  PVR-kernen forekom kun i den første kørsel — de øvrige crashede uden nogen
  ny fault, så GPU-stakken er ikke årsagen.
- Med `--js-flags=--single-threaded --jitless` stod skærmen stadig på
  spilbilledet (grønt Subway Surfers-scene, 41 % `(8,255,106)`) i nogle minutter
  før crashet — altså: spillet *renderer* fint, det er JS-motoren der dør.
- **Værktøj:** `devuan/gpu/eglplatform_x11/capture_renderer_crash.sh` (venter på
  renderer med et givet `--renderer-client-id` og fanger crashet med gdb).
- **Femte kørsel, ren software-GL (SwiftShader, 21:56-21:59):** med
  `CHROMIUM_USE_ANGLE=swiftshader CHROMIUM_UNSAFE_SWIFTSHADER=1` er hverken
  hybris-EGL'en eller vores proxy i spillet. **Brugeren kunne se drengen
  (spilfiguren) på skærmen** — det er første gang i Chromium-sporet (hardware-EGL
  stoppede ved logoet). Kort efter blev skærmen sort, og boksen blev slukket.
  Betydning: symptomet "drengen/skinnerne mangler" er altså ikke bundet til
  V8-crashet — spillet kan nå at tegne figuren. Om det skyldes software-GL'en,
  at dybde-lappen (som IKKE er aktiv uden vores proxy) ikke var i vejen, eller
  blot at spillet nåede længere i asset-load, er ikke afgjort.
- **Boksen slukkede SIG SELV (brugeren, 6. okt):** "Det var ikke mig som
  slukkede maskinen. Den lukkede selv ned." Skærmen blev sort først, og ssh
  holdt samtidig op med at svare på 192.168.0.103.
- Det peger på **strøm/termik, ikke software**: et SoC-hard-hang giver ikke
  power-off (en watchdog ville give reboot), mens en spontan nedlukning kommer
  fra PMIC'en (undervoltage/brownout) eller SoC'ens termiske beskyttelse.
  Projektet har set det før: 20. aug 2026 ("brownout-crashede den — men det var
  strømforsyningen, ikke chromium") og 25. aug 2026 ("to spil-kørsler tog boksen
  ned"). SwiftShader-kørslen er den tungeste belastning vi har prøvet: ren
  CPU-software-rendering på alle kerner.
- **Arbejdshypotese:** hvis forsyningen (eller CPU/DDR-marginen) er marginal
  under last, kan der opstå tavs hukommelseskorruption i den tungeste proces —
  det passer på vilde pointere i V8's GC, stack-smashing i worker-tråde og på at
  Firefox-vejen (2,6 fps, meget lettere last) kører stabilt.
- **Kernen kan selv slukke boksen (fundet i `lollipop_kernel_src`, 6. okt):**
  `drivers/thermal/rockchip_thermal.c` linje 762-774 logger
  `critical temperature reached(%ld C),shutting down` og kalder
  `orderly_poweroff(true)`, når temperaturen har været over `hw_shut_temp` i
  `TSADC_MAX_HW_SHUT_TEMP_COUNT` (3) målinger i træk. Standardgrænsen er
  chip-default **125 °C**; ingen DTS i træet sætter `hw-shut-temp`. Den
  generiske termiske kerne (`thermal_core.c:366`) kalder også
  `orderly_poweroff(true)` på et "critical"-trip — `rk3368-thermal.dtsi` sætter
  `cpu_crit`/`gpu_crit` til **115 °C**. Bemærk: cooling-maps (CPU-throttling ved
  80 °C) er kommenteret ud i vendortræet, så der er **ingen termisk throttling**
  — kun nedlukning.
- Konsekvens: en spontan nedlukning er altså en *kernel-mekanisme*, og den
  skriver en bestemt linje i kernelloggen. Findes den linje i
  `/var/log/kern.log` efter næste boot, er sagen afgjort (termik); gør den ikke,
  peger det på PMIC'en/strømvejen i stedet.
- **Målt på boksen (6. okt ~14:50, efter genstart på 192.168.0.135):**
  - DTB'en sætter `hw-shut-temp = 120000` (120 °C) — læst direkte fra
    `/sys/firmware/devicetree/base/tsadc@ff280000/hw-shut-temp`. Det er altså
    den grænse, software-trip'et bruger.
  - Termometeret er `/sys/rockchip_thermal/temp` (driverens eget sysfs-punkt,
    ikke den generiske thermal-klasse). Tomgang: 39-44 °C. Kalibreringen kommer
    fra eFuse (`cpu_temp_adjust = 18`).
  - **Fuld CPU-last (8 x `yes`) i 8 sekunder: 42 -> 87 -> 104 °C.** Testen blev
    afbrudt ved 100 °C. Efter stop faldt den til 52-57 °C i løbet af få sekunder.
  - Driveren kan printe rå-tallene (`echo t > /sys/rockchip_thermal/temp` giver
    `cpu code temp:[ADC-kode, temp], voltage: <µV>`). Målt:
    - tomgang, 312 MHz: kode 162, **0,925 V** -> 52 °C
    - tomgang, låst 1,2 GHz: kode 124-126, **1,225 V** -> 57-62 °C
    - under last: kode 130-131, **1,275 V** -> 94-97 °C, og 157-158 ved
      1,025 V -> 79-82 °C (samme last, spændingen svinger)
  - Formlen i driveren er `code_temp = (ADC-kode * voltage) / 1e6`: den bruger
    **CPU-regulatorens spænding**, og DVFS flytter den fra 0,925 V (312 MHz) til
    1,275 V (1,2 GHz). Derfor følger den udregnede temperatur spændingen med
    titals grader — og det sker inden for et sekund, hurtigere end nogen rigtig
    termisk masse kan følge. Den udregnede værdi er altså delvist et
    spændings-artefakt oveni den ægte opvarmning.
- **Konklusion:** under vedvarende fuld last krydser den udregnede temperatur
  120 °C, og kernen kalder `orderly_poweroff(true)` -> boksen slukker sig selv.
  Det er præcis det observerede forløb (SwiftShader belaster alle 8 kerner).
  Brugerens mavefornemmelse var rigtig: det er kernen — nærmere bestemt
  vendorens spændingsafhængige temperaturformel kombineret med DVFS.
- **Termik og crash er to forskellige problemer (målt 15:00-15:25):**
  - Spilkørsel på hardware-EGL med fri governor: temperaturen nåede 97 °C et
    øjeblik, men boksen blev oppe — og rendereren crashede efter ~1,5 min.
    Spillet alene udløser altså IKKE termik-trip'et; det kræver alle-kerner-last.
  - **Fair test (brugerens indvending):** spillet kørt ved **816 MHz** (låst
    frekvens, hvor temperaturen lå roligt på 59-64 °C). Det crashede OGSÅ — og
    ifølge brugeren mens det stadig stod i "50 % loadet", altså **uden 3D**.
    Crash-tallet gik fra 45 til 46 dumper. Dermed er frekvens-/termik-hypotesen
    for crashet **afvist**: det sker både ved 1,2 GHz og 816 MHz, og det kræver
    hverken top-OPP'en eller 3D-rendering.
  - **Kontrolleret modtest:** `devuan/gpu/eglplatform_x11/v8_gc_stress.html`
    (tunge JS-allokeringer + 300 GL-kald pr. frame) kørte 5 min ved 1,2 GHz med
    16,4 mio. allokeringer **uden crash**. Ren V8-GC-belastning er altså ikke nok
    — spillet gør noget mere (tunge tekstur-/asset-pipelines og meget mere
    GPU<->renderer-IPC).

### uBlock Origin Lite på Chromium (7. okt) — og hvad der blokerer 50 %-stop'et

- **Klassisk uBlock Origin (MV2) kan ikke bruges i Chromium 150.** I stedet er
  **uBlock Origin Lite 2026.1006.1931** (MV3) hentet fra
  `uBlockOrigin/uBOL-home` og lagt på boksen i
  `/home/kristian/ublock-origin-lite` (924 filer, 35.483.328 bytes — samme som
  den udpakkede zip på laptoppen). Den indlæses som **upakket udvidelse**:
  `CHROMIUM_EXTRA_FLAGS='--load-extension=/home/kristian/ublock-origin-lite|--disable-extensions-except=/home/kristian/ublock-origin-lite'`.
  Den er verificeret aktiv (CDP viser dens `service_worker` på
  `chrome-extension://efibmdkmadlglmjplfpeonnhblbfncip/js/background.js`), og
  annonce-iframes er væk (0 mål mod 23-24 renderere uden blocker).
- **50 %-stop'et er Pokis reklame-pause, ikke spillets indlæsning.** Sidens
  tekst (læst via CDP) er: `We'll be back after this short break` /
  `Preparing...`. Med uBOL blokeret kan annoncen ikke hentes, og Poki venter i
  det uendelige på sin ad-break — spillets egen JS kører aldrig (0
  `Cache size`-beskeder mod hundredvis i de kørsler hvor spillet kom i gang).
- **Poki-siden kan ikke omgås:** `https://games.poki.com/<game-id>` svarer med
  "Sorry! Subway Surfers has moved / Click here to play!" (linker tilbage til
  `poki.com/en/g/subway-surfers`), og selve spilbygget på
  `https://<game-id>.gdn.poki.com/<version>/index.html?...` bliver afvist af
  Cloudflare ("Sorry, you have been blocked" / "You are unable to access
  poki.com").
- **Betydning for den fair sammenligning:** Firefox-vejen har klassisk uBO, som
  også fjerner selve reklame-pause-logikken (scriptlets). uBOL i standard
  ("basic") blokerer kun annoncedomænerne, så siden står fast. Skal Chromium
  matche, kræver det enten uBOL i "complete"-mode med scriptlets (slås til i
  dens dashboard — en fysisk klik-handling) eller tilsvarende regler.
- **Observation der skal undersøges:** `/dev/fb0` kan kun læses til 2.073.600
  bytes (1920x540x16), selvom HDMI-modus er `1920x1080p-60`, X melder
  1920x1080 og fb-varen siger `1920,1080` (4.147.200 bytes). Vores fb-dumps
  dækker altså kun den øverste halvdel af skærmen — det påvirker `fb_fps` og
  `rgb565_to_png.py` og bør klares.
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
- [udført] Crashpad-minidumperne analyseret; Debians debuginfod brugt til at
  symbolisere adresserne (build-id `f0e2662b…`). (~21:40)
- [udført] Fire isolationskørsler (annonceblokering, ingen samtidig markering,
  ingen V8-baggrundstråde, ingen JIT) — alle crashede igen i V8. (~21:50)
- [afventer] Beslutning med brugeren: (a) afgør hardware vs. software ved at
  køre samme Chromium på boks 2, (b) RAM-test af boks 1, eller (c) parkér
  Chromium-sporet og gå tilbage til Firefox-vejen (spilbar, fps-loftet åbent).
- [udført] Femte kørsel med ren software-GL (SwiftShader): brugeren så drengen;
  skærmen blev sort, og boksen slukkede SIG SELV kort efter. (~21:59)
- [afventer] Fysisk: stærkere 5V-forsyning til boksen (helst 3A + godt kabel),
  og tjek om den bliver varm. Det er den førende hypotese for både V8-crashene
  og den spontane nedlukning.
- [afventer] Ved næste boot: læs den forrige boots kernellog
  (`/var/log/kern.log`) for termiske/undervoltage-linjer, og tjek
  `/sys/class/thermal/*/temp`. Software-mitigering der kan bekræfte teorien:
  lås CPU-frekvensen lavt (cpufreq `userspace`) og se om crashene holder op.
- [udført] Boksen oppe igen på 192.168.0.135. Kernelloggen indeholder **ingen**
  `critical temperature reached` — men til gengæld stoppede logningen 55 min før
  døden (sidste linje 21:04:41, døden ~21:59), så loggen kan hverken bekræfte
  eller afvise trippet. Derfor `devuan/templog.sh` (sort boks). (~14:48)
- [udført] Målt: `hw-shut-temp = 120 °C` i DTB'en; fuld CPU-last driver den
  udregnede temperatur fra 42 til 104 °C på 8 sekunder; formlen bruger
  CPU-spændingen (0,925 -> 1,275 V), så titals grader af læsningen er et
  DVFS-artefakt. (~14:52)
- [aftalt] CPU-governoren er gendannet til `interactive`, boksen er i ro
  (312 MHz, 52 °C), og der ligger ingen efterladt belastning. (~14:54)
- [udført] Ligevægtstest: fuld last ved låst 816 MHz stabiliserer på **77 °C**
  (3 min), og boksen falder til 62 °C bagefter. Kølingen er fin nok ved den
  frekvens. (~15:00)
- [udført] Spilkørsel på hardware-EGL: 97 °C i top, ingen nedlukning, men
  renderer-crash efter ~1,5 min. (~15:03)
- [udført] Fair test: spillet ved 816 MHz crashede også, mens det stod i 50 %
  load (uden 3D). Frekvens-/termik-hypotesen for crashet er afvist. (~15:23)
- [udført] Kontrolleret modtest: `v8_gc_stress.html` (tung JS-allokering + GL) i
  5 min ved 1,2 GHz uden crash. (~15:21)
- [afventer] Næste isolationsforsøg: spillet med **SwiftShader + CPU låst på
  816 MHz** (undgår termik-nedlukningen). Det er den sidste kørsel hvor spillet
  faktisk kom videre (brugeren så drengen) — og den afgør om det er
  hybris-/EGL-vejen eller Chromium selv.
- [udført] SwiftShader + 816 MHz: **ingen crash** i 5 min, skærmen opdaterede
  (fb-hash skiftede) — men spillet stod i Pokis reklame-pause. (~15:40)
- [udført] uBlock Origin Lite (MV3) hentet og installeret på boksen; verificeret
  aktiv. Annonce-iframes væk, men Pokis reklame-pause hænger stadig. (~15:55)
- [udført] Direkte game-URL'er afprøvet: games.poki.com ("has moved") og
  gdn.poki.com-bygget (Cloudflare-blokering) — ingen vej uden om Poki-siden.
  (~16:00)
- [afventer] Beslutning med brugeren: (a) uBOL i "complete"-mode med scriptlets
  (kræver et fysisk klik i dashbordet på boksen) så reklame-pausen springes
  over, (b) fortsætte Chromium-sporet på V8-crashet, eller (c) parkere Chromium
  og gå tilbage til Firefox-vejen, hvor spillet kører.
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

1. Afgør om det er hardware eller software: kør samme Chromium-kørsel på
   **boks 2** (samme model, samme image). Crasher den der også, er det
   Chromium 150 + denne 3.10-kernel; crasher den ikke, er boks 1 mistænkelig
   (jf. boksens historik med hard lockups, ext2-korruption og et
   strømforsynings-uheld).
2. Hvis boks 1 skal renses: kør `memtester` på den (kræver at Chromium stoppes).
3. Ellers: parkér Chromium-sporet. Firefox-vejen kører spillet (2,6 fps) og er
   den vej der virker; fps-loftet er det åbne spor der.
4. Zero-copy-spørgsmålet fra 2. okt er sat til side indtil videre — det kan
   ikke besvares mens rendereren dør.

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
