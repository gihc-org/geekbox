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
`/root`). De nyeste er tilføjet 25. sep 2026 (S1-sessionen).

| Værktøj | Hvad det gør |
|---|---|
| `devuan/box.sh` | tynd ssh/scp-wrapper til boksen (`<kommando>`, `--put`, `--get`); finder IP'en selv |
| `fb_fps.c` | skærmens faktiske opdateringsrate (følger ypan/blok-ændringer). **Brug `POLL=20`** — med 200 ms poll kan den ikke måle over ~2-5/s (fælde 49) |
| `fb_bench.c` | rå skrivehastighed til `/dev/fb0` (helt frame, rektangler, memset) |
| `xput_bench.c` | X-serverens egen presentvej (`XPutImage`, XSync pr. frame) |
| `x_focus.c`, `x_resize.c` | sætter fokus på hovedvinduet / skalerer det (`-big` = største Firefox-vindue) |
| `cyan_ab_run.sh` | én A/B-målekørsel: ren profil, Firefox med proxy-opskriften, BiDi-preload, fokus, `fb_fps`, rAF- og GL-tælling, canvas-geometri |
| `s1_variants.sh` | S1-værktøjskæden: `backup`, `prefs V1-V5`, `restore <ts>`, `run`, `wait`, `result` — lægger `s1_prefs/*.user.js` ind i profilen og starter målingen afkoblet |
| `s1_snapshot.sh` | kører PÅ boksen: proces-snapshot 75 s inde i kørslen (GPU-proces? hvem har hybris-libEGL? GL-aktivitet i probe.log). Nødvendigt fordi `MOZ_LOG`-modulerne Compositor/LayerManager/WebRender er tavse i ESR-140 (fælde 53) |
| `bidi_cyan.py` | BiDi-klient: `preload` (alpha-shim + rAF-logning), `goto`, `domcheck` (canvas-geometri) |
| `raf_test.html`, `raf_test_full.html` | kontrolsider: flyt-boks (uofficiel til rate-måling) og fuldt gentegnet baggrund (bruges som referenceramme) |
| `kill_bidi.sh` | lukker en hængt BiDi-klient (fælde 42) |

**Regel fra 25. sep 2026:** mål altid en baseline i *samme* session som
forsøgene — ydelsen på boksen drev ~25 % i løbet af en aften (fælde 56).

---
