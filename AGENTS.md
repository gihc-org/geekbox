# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd og ssh (dropbear) virker. **GPU + WebGL virker:** DDK 1.5@3830101 kører på en genbygget 3.10-kernel med 1.5-KM, 1.5-userspace (32-bit) og 64-bit `pvrsrvctl`. Subway Surfers er spilbart i Firefox med vnext12 `dcc0a68f` (den tunge proxy), alpha-shim via BiDi-preload og `CYAN_DEPTHCLEAR=1`; standarden er 960x540 med cirka 3-4 skærmopdateringer/s. Lyd og input virker via fast-play-launcheren. Boks 2 blev flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Scripts i `devuan/`.

**Aktuelt spor (12. nov 2026): Stabilitet i Firefox-fast-play** på branch
`firefox-fps-tuning`. Den tidligere tab-crash efter cirka 20-22 minutter var
forårsaget af en fence-fd-lækage i `eglplatform_x11.cpp`: hver præsenteret
frame efterlod et `sync_fence`-fd i Firefox-parenten, indtil grænsen på 4096
descriptors blev nået. `queueBuffer()` og `cancelBuffer()` lukker nu fencen.

Fixet holdt gennem en fem timer lang test med `Web Content` intakt, ingen
tab-crash og kun 3-4 sync-fences. Brugeren spillede med flere pauser, og
Firefox kørte fortsat efter næsten seks timer. Ressourcetallene var stabile
eller faldende. En enkeltstående `threaded-ml`-segmentation efter cirka fire
timer i den foregående kørsel blev ikke reproduceret.

Stabilitets- og UI-sporet er bestået for det praktiske spilflow. Næste spor er
besluttet i denne rækkefølge: et samlet bring-up-flow nu og derefter
FPS-profilering. Læs den nyeste handover i `TODO.md`,
`docs/log/2026-11-12-stability.md`, `docs/log/2026-11-11-stability.md` og
`docs/faeller.md` fælde 41-68.
Chromium-sporet er parkeret: lyd/logik kan køre, men 3D-verdenen mangler,
native depth-clear giver GPU-wedge, og der er separate
Maglev-/JITless-crashes.

**Firefox/FPS-baggrund:** den tunge vnext12-proxy må bruges når spillet skal
kunne ses; `CYAN_LIGHT=1` frøser præsentationen og må kun bruges til isolerede
målinger. Det målte fps-loft ligger i Firefox' software-compositing og er
arealbestemt, ikke i driverens draw-kald. Billige prefs,
`layout.frame_rate`, canvas-nedskalering, SWGL og hardware-WebRender er
allerede målt som blindgyder. Blå glitches og den nye mønt-/bogstavfejl er
udskudt. Kernel clone3-fix er bygget og flashet; næste kernel-byg er fortsat
planlagt med `CONFIG_ANDROID_PARANOID_NETWORK` fra plus bcmdhd. NTP-sporet er
åbent. Overblik: `OVERBLIK.md`.

## Agentens rolle

Du er en elektronik/computer ekspert

## Samarbejdet med brugeren

- **Spørg kun brugeren om noget fysisk:** tænd/sluk boksen, sæt strøm eller
  kabel i, klik eller tast på boksen/TV'et, se på skærmen, rør ved hardware.
- **Alt andet kører agenten selv** — ssh/scp til boksen, filer, git, byg,
  flash-værktøjer, målinger, `dmesg`-tjek. Spørg ikke om lov til en kommando,
  bare fordi den kræver forhøjede rettigheder eller går ud af sandkassen.
- Undtagelser: destruktive eller svært reversible handlinger (flash, sletning,
  overskrivning af data på kortet) og handlinger uden for opgavens rammer —
  dem vendes med brugeren først.
- Ved tvivl: tag det sikre, reversible skridt, sig hvad der blev gjort, og nævn
  antagelsen i svaret.

## Dokumentation undervejs (beslutninger + status)

- Hold en løbende sektion **"Aftaler og beslutninger"** øverst i dagens session-notat (`docs/log/YYYY-MM-DD-<emne>.md`) og opdatér den SAMME TIME noget aftales — ikke først ved checkpoint eller sessionslut.
- Hver post skrives som `- [status] <hvad> (<tidspunkt>)` med status: `aftalt` / `udført` / `afventer` / `foreslået` (idé, IKKE plan).
- Skel eksplicit mellem **aftalt** (skal ind i "Næste skridt" + TODO.md) og **foreslået** (kun idé; må ikke behandles som plan uden brugerens godkendelse).
- Hold afsnittet **"Status i ét blik"** øverst i notatet løbende opdateret, så brugeren kan se projektets tilstand uden at læse samtalen.
- Når brugeren spørger om status: giv 1 kort statusafsnit i svaret + peg på notatets "Status i ét blik" og "Aftaler og beslutninger".
- Ufuldstændige beslutninger (manglende detalje der kan ændre planen) skrives som `afventer` og stilles til brugeren — de må ikke forsvinde i samtalen.
- Ved sessionslut/komprimering: kontrollér FØR handover at alle aftaler står i notatet (diff mellem "aftalt i samtalen" og "dokumenteret"); ret mangler og sig det højt.
- Når en plan/beslutning ændres, opdatér også TODO.md og den relevante fil under `docs/` samme time (korte pointere, ikke dubletter).
- **Git-commits:** hver commit hvor en AI-agent har bidraget slutter med en
  `Co-authored-by`-trailer nederst i *body'en* — efter en tom linje, aldrig i
  emnet. Se `~/projects/guidelines/code-review.md` under "AI-medforfatterskab".
  Formen er `Co-authored-by: <værktøj>/<model> <noreply-adresse>`, fx
  `Co-authored-by: Codex/DeepSeek-flash v4 <noreply@openai.com>`.
  Emne-suffikset `[agent:model]` er afløst 17. sep 2026; det står stadig i
  ældre commits, men skal ikke kopieres.
  Værktøj = det du kører som (`codex`), model = den FAKTISKE model-id for DEN
  PÅGÆLDENDE session. Model-id skal hentes fra den aktuelle sessions egne
  oplysninger — du må IKKE arve/antage model-id fra tidligere sessioner,
  dokumenter eller commit-historik (fejl begået 26. aug 2026: `[codex:gpt-5]`
  brugt i en deepseek-v4-flash-session). Ved tvivl: spørg brugeren. Ret aldrig
  ældre commits' tags uden brugerens godkendelse (omskriver historik). Notér
  desuden agent/model øverst i session-notatet.
- **Tegnsæt i dokumentation (.md):** brug kun tegn der findes i DejaVu Sans /
  DejaVu Sans Mono (pandoc/xelatex bygger PDF'erne med dem). Ingen emojier eller
  symbol-ikoner (fx U+2705/U+274C/U+1F389/U+1F464/U+1F916); danske tegn (æøå),
  almindelig tegnsætning, pilen `→`, § og tal er OK. Tjek før commit med
  `bash devuan/check_no_emoji.sh` (hele repoet); pre-commit-hooken
  `devuan/hooks/pre-commit` (installeret i .git/hooks) kører samme regel på
  staged .md-filer — ret hits før commit.
    Installation af hooken (én gang pr. klon): `bash devuan/hooks/install.sh`.
    Den lægger en rigtig fil i `.git/hooks/` frem for et symlink, så en branch
    uden `devuan/hooks/pre-commit` giver en advarsel i stedet for at slå reglen
    fra i stilhed.

## Checkpoint- og tråd-disciplin

- Ved milepæle (rodårsag fundet, virkende fix, måling fanget, dokument opdateret) eller når samtalen har været komprimeret 2 gange: opdater et kort session-notat (`docs/log/YYYY-MM-DD-<emne>.md`) med: hvad vi ved, hvad der er forsøgt, nuværende hypotese, næste skridt og de kommandoer der virker.
- Hold notatet kort og kommando-tungt; læg vigtige fakta i repo-filer (`docs/`), ikke kun i samtalehistorikken.
- Hvis du gentager dig selv, spørger om ting du allerede har fastslået, eller genkører kommandoer: sig det højt og foreslå eksplicit at starte en ny tråd, der begynder med "Læs <notatet> og fortsæt derfra".
