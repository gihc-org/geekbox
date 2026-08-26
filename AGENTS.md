# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd, ssh (dropbear) — alt virker. **GPU + WebGL virker (26. aug 2026):** DDK 1.5@3830101 kører på en genbygget 3.10-kernel (PVR fra, TRACING, VT, compat-403-fix) med 1.5-KM `.ko` + 1.5-UM (32-bit) + 64-bit pvrsrvctl — `GL_VERSION = "OpenGL ES 3.1 build 1.5@3830101"`, `shader_ext_test` draw_buffers OK (ES2+ES3), WebGL OK i firefox-esr. Subway Surfers (poki.com) er STADIG blokeret: præsentationen til skærmen fejler (gralloc-lock EINVAL i Firefox' GPU-proces) — shader-blokaderne (GL_EXT_frag_depth + WR cs_blur) er løst via proxy-omskrivning. Se DOKUMENTATION.md §5.15, HAANDBOG.md fælde 16+ og TODO.md. Boks 2 flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Scripts i `devuan/`.

**Aktuelt spor:** `devuan/gpu/GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md` (gralloc-lock-EINVAL LØST: /dev/sw_sync-permissions + x11ws usage 0x3; poki-load-crash LØST: glesv2-proxyen brød WebGL1/ES1, behold original libGLESv2; spillet kører med shim-udvidelse alpha:true + loseContext-block + layers.acceleration.disabled=true; RESTERENDE: 3D-scenen renderer cyan — spillet sender ikke scene-draws på 1.5-stakken; næste: afgør om spillet deaktiverer scenen via kapabilitets-check; næste kernel-byg: CONFIG_ANDROID_PARANOID_NETWORK fra + bcmdhd; NTP-sporet åbent) — opdater denne linje når et nyt spor starter.

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

## Checkpoint- og tråd-disciplin

- Ved milepæle (rodårsag fundet, virkende fix, måling fanget, dokument opdateret) eller når samtalen har været komprimeret 2 gange: opdater et kort session-notat (`devuan/gpu/<EMNE>-SESSION-NOTAT-YYYY-MM-DD.md`) med: hvad vi ved, hvad der er forsøgt, nuværende hypotese, næste skridt og de kommandoer der virker.
- Hold notatet kort og kommando-tungt; læg vigtige fakta i repo-filer (`DOKUMENTATION.md` / `HAANDBOG.md`), ikke kun i samtalehistorikken.
- Hvis du gentager dig selv, spørger om ting du allerede har fastslået, eller genkører kommandoer: sig det højt og foreslå eksplicit at starte en ny tråd, der begynder med "Læs <notatet> og fortsæt derfra".
