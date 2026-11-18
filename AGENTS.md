# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd og ssh (dropbear) virker. **GPU + WebGL virker:** DDK 1.5@3830101 kører på en genbygget 3.10-kernel med 1.5-KM, 1.5-userspace (32-bit) og 64-bit `pvrsrvctl`. Subway Surfers er stabilt spilbart i Firefox med vnext12 `dcc0a68f`, alpha-shim via BiDi-preload og `CYAN_DEPTHCLEAR=1`. Standarden er 960x540 med automatisk canvas 0,75 og cirka 8 skærmopdateringer/s. Lyd, input og lokal ét-kliks start efter genstart virker. Boks 2 blev flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Scripts i `devuan/`.

**Aktuelt spor (18. nov 2026): Stabil boot og flash af næste boks** på branch
`firefox-fps-tuning`. Den aktuelle boks har stabil eth0-MAC udledt fra
CPU-serien, korrekt tid efter strømcyklus og fungerende Ethernet/WiFi.
Chrony/NTP virker fordi `_chrony` har primær GID `3003/inet`; numeriske
kilder undgår tidlig DNS. HTTP-`Date` via gatewayen er fallback. Subway
Surfers kan startes via skrivebordsikonet.

Næste milepæl er at bygge et nyt base-image, flashe den anden boks og køre
`devuan/gpu/provision_game_box.sh` med den nuværende boks som kilde. Den
fælles game-runtime indeholder GPU-platform, Firefox-shims, hjælpescripts og
en minimal spilprofil. Accepttesten er korrekt tid/NTP, stabil MAC, lyd,
input og ét-kliks spilstart på den nye boks.

Læs den nyeste handover i `TODO.md`,
`docs/log/2026-11-18-dokumentationsrevision.md`,
`docs/log/2026-11-16-boot-stability.md` og
`docs/log/2026-11-16-launcher.md`, samt `docs/boksen/flash.md` §11.
Generaliseringen til fælles platform, Firefox-runner og versionsstyret
spilprofil er fortsat den efterfølgende milepæl. Fortsæt ikke med
Subway-specifik FPS-tuning før flash-flowet er verificeret.
Chromium-sporet er parkeret: lyd/logik kan køre, men 3D-verdenen mangler,
native depth-clear giver GPU-wedge, og der er separate
Maglev-/JITless-crashes.

**Firefox/FPS-baggrund:** den tunge vnext12-proxy må bruges når spillet skal
kunne ses; `CYAN_LIGHT=1` frøser præsentationen og må kun bruges til isolerede
målinger. Profilering udpegede RGB565-præsentationen som en stor fælles
flaskehals; NEON og fire konverteringstråde reducerede `present()` fra cirka
261 ms til cirka 29 ms. `layout.frame_rate`, SWGL og hardware-WebRender er
fortsat blindgyder, mens canvas 0,75 nu er den accepterede webspil-variant.
Blå glitches og mønt-/bogstavfejlen er udskudt. Kernel clone3-fix er bygget og
flashet. `bcmdhd`/WiFi virker, og NTP-sporet er lukket.
Overblik: `OVERBLIK.md`.

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
