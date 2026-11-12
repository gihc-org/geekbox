# Værktøjskassen

## 6. Værktøjskassen

| Script | Hvad det gør |
|---|---|
| `devuan/01_build_rootfs.sh` | bygger Devuan-rodfilsystemet fra bunden |
| `devuan/07_desktop_audio.sh` | skrivebord, lyd, bruger, tapet, overscan-opsætning |
| `devuan/extra_packages.sh` | pakkelisten: firefox, sudo, rsyslog, locale m.m. |
| `devuan/09_make_emmc_img.sh` | bygger imaget og sikrer alt det en boks ikke kan undvære |
| `devuan/testflash.sh` | bygger + flasher, med pause til loader-tilstand |
| `devuan/find_box.sh` | finder boksen på netværket (dens IP skifter hver boot) |
| `devuan/08_network_manager.sh` | NetworkManager på et **SD-kort** i læseren, og migrering af kendte wifi-netværk. eMMC-flowet får NM via `extra_packages.sh` + `09` |
| `devuan/emmc_first_boot.sh` | swapfil på 2 GB efter flash |
| `devuan/fb_overscan.py` | skrumper billedet, så fjernsynets beskæring ikke rammer noget |
| `devuan/patch_uboot_logo.py` | ændrer DT-flag i imaget (til eksperimenter) |
| `devuan/rollback.sh` | ruller DTB-flaget tilbage og flasher, hvis en boks ikke booter |

## 6.1 FPS- og cyan-scene-måleværktøjer

Alt her ligger i `devuan/gpu/eglplatform_x11/` (og kopieres til boksens
`/root`). FPS-værktøjerne kom til i september 2026, Chromium-sporet 30. sep
og fast-play-/lydværktøjerne 7. nov 2026.

| Værktøj | Hvad det gør |
|---|---|
| `devuan/box.sh` | tynd ssh/scp-wrapper til boksen (`<kommando>`, `--put`, `--get`); finder IP'en selv |
| `devuan/gpu/eglplatform_x11/bringup_after_power.sh` | idempotent bring-up efter strøm-cyklus: venter på ssh, ur, `insmod` + `gpu_up.sh`, `/dev/sw_sync`, proxy vnext12, bindapi-lap + VT tilbage til X, kontrolsider til `/tmp`, GL-verifikation og md5 mod repoet |
| `fb_fps.c` | skærmens faktiske opdateringsrate (følger ypan/blok-ændringer). **Brug `POLL=20`** — med 200 ms poll kan den ikke måle over ~2-5/s (fælde 49) |
| `fb_bench.c` | rå skrivehastighed til `/dev/fb0` (helt frame, rektangler, memset) |
| `xput_bench.c` | X-serverens egen presentvej (`XPutImage`, XSync pr. frame) |
| `x_focus.c`, `x_resize.c` | sætter fokus på hovedvinduet / skalerer det (`-big` = største Firefox-vindue) |
| `cyan_ab_run.sh` | én A/B-målekørsel: ren profil, Firefox med proxy-opskriften, BiDi-preload, fokus, `fb_fps`, rAF- og GL-tælling, canvas-geometri |
| `play_fast_game.sh` | fast-play-launcher: standard 960x540 eller `WIN="640 360"`, sikrer `xulstore.json`, starter den kendte Firefox-opsætning, indlæser spillet via minimal BiDi-preload, verificerer renderflade/WebGL og efterlader Firefox kørende |
| `ensure_audio.sh` | kontrollerer at PulseAudio bruger `/opt/alsa-da` og `alsa_output.dmixer`; genstarter serveren kontrolleret hvis den er faldet tilbage til `auto_null` |
| `kill_firefox.sh` | stopper Firefox-parent og alle content-/helperprocesser via præcis argv[0]-matchning; undgår orphans og SSH-fælden ved `pkill -f` |
| `stability_monitor.sh` | logger Firefox-proces-træ, hukommelse, temperatur og crashfiler under en længere test; markerer når parenten eller en aktiv content-process forsvinder |
| `start_game_all.sh` | lokal idempotent bring-up af GPU, vnext12-proxy, bind-api-lap, VT og lyd før fast-play; kræver root |
| `subway-surfers-launcher.sh` | åbner den lokale bring-up i lxterminal og kalder den afgrænsede sudo-regel |
| `subway-surfers.desktop` | skrivebords- og menuikon til den samlede lokale spilstart |
| `s1_variants.sh` | S1-værktøjskæden: `backup`, `prefs V1-V5`, `restore <ts>`, `run`, `wait`, `result` — lægger `s1_prefs/*.user.js` ind i profilen og starter målingen afkoblet |
| `s1_prefs/V1-V5.user.js` | de fem kompositor-varianter fra S1 (V1 baseline, V2 SWGL, V3 WebRender/GPU, V4 uden GPU-proces, V5 WR/GPU uden GPU-proces) |
| `s1_snapshot.sh` | kører PÅ boksen: proces-snapshot 75 s inde i kørslen (GPU-proces? hvem har hybris-libEGL? GL-aktivitet i probe.log). Nødvendigt fordi `MOZ_LOG`-modulerne Compositor/LayerManager/WebRender er tavse i ESR-140 (fælde 53) |
| `prof_top.py` | læser en Gecko-profiler-JSON (S2) og viser top-funktioner pr. tråd; håndterer profil-format v31 og mapper rå adresser til bibliotek via profilens `libs` |
| `parse_nsrefresh_pairs.py` | matcher `Allocating`/`Completed transaction` pr. PID, refresh-driver og transaction-id; viser p50/p90/p99 og rate pr. minut |
| `salvage_gecko_profile.py` | lukker et afkortet Gecko-profil-JSON syntaktisk, når Firefox crasher under skrivning; redder kun data der faktisk nåede filen |
| `bidi_cyan.py` | BiDi-klient: `preload` (alpha-shim + rAF-logning), `goto`, `domcheck`, `close`, `PLAY_AT` til automatisk spilstart og `SUPPORT_POLL`/`SUPPORT_OUT` til at hente `about:support` |
| `raf_test.html`, `raf_test_full.html` | kontrolsider: flyt-boks (uofficiel til rate-måling) og fuldt gentegnet baggrund (bruges som referenceramme) |
| `kill_bidi.sh` | lukker en hængt BiDi-klient (fælde 42) |
| `start_chromium_probe.sh` | starter Chromium 150 mod hybris-miljøet med valgfri `--use-gl`/`--use-angle`, egen profil og log; brug `/usr/lib/chromium/chromium` for at undgå Debian-wrapperens MV2-extension-load |
| `chromium_cdp.py` | lille CDP-klient til Chromium (`list`, `eval`, `goto`, `game`); `game` blokerer reklamedomæner og fjerner `failIfMajorPerformanceCaveat` for SwiftShader-testen. Kræver en ssh-tunnel til port 9223 |

**Regel fra 25. sep 2026:** mål altid en baseline i *samme* session som
forsøgene — ydelsen på boksen drev ~25 % i løbet af en aften (fælde 56).

**Profil-dump fra 5. nov 2026:** Foretræk `SIGUSR2`, ikke
`MOZ_PROFILER_SHUTDOWN`. Runtime-dumpet skriver
`/home/kristian/Downloads/profile_0_<pid>.json` og undgår Firefox'
pthread-mutex-crash under shutdown. Se fælde 58.

**Vinduesstørrelse fra 5. nov 2026:** Sæt `WIN="<bredde> <højde>"` på
`cyan_ab_run.sh`. Scriptet patcher `xulstore.json` før start og verificerer
den faktiske geometri; efterfølgende `x_resize` på et maksimeret vindue er
ikke pålideligt (fælde 66).

**Fast play fra 7. nov 2026:** `play_fast_game.sh` er den daglige launcher.
Den er ikke en målekørsel, kræver `RESTART=1` hvis Firefox allerede kører,
og lægger logs/screenshot i `/tmp/play_fast_game.*`. Verificér renderfladen,
ikke Openbox' ydre klientramme (fælde 67).

**Firefox-oprydning fra 8. nov 2026:** Brug `kill_firefox.sh`, ikke kun
`pkill -x firefox-esr`. Sidstnævnte efterlod WebExtensions med parent PID 1
og høj CPU (fælde 68). Launcheren kalder den nye helper automatisk.

---
