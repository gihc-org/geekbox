# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd, ssh (dropbear) — alt virker. **GPU + WebGL virker (26. aug 2026):** DDK 1.5@3830101 kører på en genbygget 3.10-kernel (PVR fra, TRACING, VT, compat-403-fix) med 1.5-KM `.ko` + 1.5-UM (32-bit) + 64-bit pvrsrvctl — `GL_VERSION = "OpenGL ES 3.1 build 1.5@3830101"`, `shader_ext_test` draw_buffers OK (ES2+ES3), WebGL OK i firefox-esr. Subway Surfers (poki.com) er STADIG blokeret: præsentationen til skærmen fejler (gralloc-lock EINVAL i Firefox' GPU-proces) — shader-blokaderne (GL_EXT_frag_depth + WR cs_blur) er løst via proxy-omskrivning. Se DOKUMENTATION.md §5.15, HAANDBOG.md fælde 16+ og TODO.md. Boks 2 flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Scripts i `devuan/`.

**Aktuelt spor:** `devuan/gpu/CYAN-CLONE3-SESSION-NOTAT-2026-09-06.md` (cyan-scene: scene-draws sker HELE TIDEN — 500–13.000 verts/kald via glDrawElementsInstanced — men efter-draw-readback viser FBO ensartet himmel-cyan → store meshes skriver 0 pixels; poki-frit replay-probe beviser at spillets prog7-shaders + rasterisering virker på 1.5; næste: fang glDrawBuffers/GL_DRAW_BUFFER0..3 + evt. vertex-buffer-snapshots på en vellykket load for at afgøre attachment/frustum). Kernel clone3-fix (syscall 435→ENOSYS) bygget og flashet 6. sep — Firefox telemetri-crash væk. Næste kernel-byg (planlagt): CONFIG_ANDROID_PARANOID_NETWORK fra + bcmdhd. NTP-sporet åbent. Overblik: `OVERBLIK.md` — opdater denne linje når et nyt spor starter.

## Agentens rolle

Du er en elektronik/computer ekspert

## Dokumentation undervejs (beslutninger + status)

- Hold en løbende sektion **"Aftaler og beslutninger"** øverst i dagens session-notat (`devuan/gpu/<EMNE>-SESSION-NOTAT-YYYY-MM-DD.md`) og opdatér den SAMME TIME noget aftales — ikke først ved checkpoint eller sessionslut.
- Hver post skrives som `- [status] <hvad> (<tidspunkt>)` med status: `aftalt` / `udført` / `afventer` / `foreslået` (idé, IKKE plan).
- Skel eksplicit mellem **aftalt** (skal ind i "Næste skridt" + DOKUMENTATION.md/TODO.md) og **foreslået** (kun idé; må ikke behandles som plan uden brugerens godkendelse).
- Hold afsnittet **"Status i ét blik"** øverst i notatet løbende opdateret, så brugeren kan se projektets tilstand uden at læse samtalen.
- Når brugeren spørger om status: giv 1 kort statusafsnit i svaret + peg på notatets "Status i ét blik" og "Aftaler og beslutninger".
- Ufuldstændige beslutninger (manglende detalje der kan ændre planen) skrives som `afventer` og stilles til brugeren — de må ikke forsvinde i samtalen.
- Ved sessionslut/komprimering: kontrollér FØR handover at alle aftaler står i notatet (diff mellem "aftalt i samtalen" og "dokumenteret"); ret mangler og sig det højt.
- Når en plan/beslutning ændres, opdatér også TODO.md og DOKUMENTATION.md samme time (korte pointere, ikke dubletter).
- **Git-commits:** hver commit slutter med et tag på formen `[agent:model]` — agent =
  værktøjet (`codex`), model = den FAKTISKE model-id for DEN PÅGÆLDENDE session
  (fx `[codex:deepseek-v4-flash]`). Model-id skal hentes fra den aktuelle sessions
  egne oplysninger — du må IKKE arve/antage model-id fra tidligere sessioner,
  dokumenter eller commit-historik (fejl begået 26. aug 2026: `[codex:gpt-5]` brugt
  i en deepseek-v4-flash-session). Ved tvivl: spørg brugeren. Ret aldrig ældre
  commits' tags uden brugerens godkendelse (omskriver historik). Notér desuden
  agent/model øverst i session-notatet.
- **Tegnsæt i dokumentation (.md):** brug kun tegn der findes i DejaVu Sans /
  DejaVu Sans Mono (pandoc/xelatex bygger PDF'erne med dem). Ingen emojier eller
  symbol-ikoner (fx U+2705/U+274C/U+1F389/U+1F464/U+1F916); danske tegn (æøå),
  almindelig tegnsætning, pilen `→`, § og tal er OK. Tjek før commit med
  `bash devuan/check_no_emoji.sh` (hele repoet); pre-commit-hooken
  `devuan/hooks/pre-commit` (installeret i .git/hooks) kører samme regel på
  staged .md-filer — ret hits før commit.
  Installation af hooken (én gang pr. klon):
  `ln -sfn ../../devuan/hooks/pre-commit .git/hooks/pre-commit`.

## Checkpoint- og tråd-disciplin

- Ved milepæle (rodårsag fundet, virkende fix, måling fanget, dokument opdateret) eller når samtalen har været komprimeret 2 gange: opdater et kort session-notat (`devuan/gpu/<EMNE>-SESSION-NOTAT-YYYY-MM-DD.md`) med: hvad vi ved, hvad der er forsøgt, nuværende hypotese, næste skridt og de kommandoer der virker.
- Hold notatet kort og kommando-tungt; læg vigtige fakta i repo-filer (`DOKUMENTATION.md` / `HAANDBOG.md`), ikke kun i samtalehistorikken.
- Hvis du gentager dig selv, spørger om ting du allerede har fastslået, eller genkører kommandoer: sig det højt og foreslå eksplicit at starte en ny tråd, der begynder med "Læs <notatet> og fortsæt derfra".
