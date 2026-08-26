# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd, ssh (dropbear) — alt virker. WebGL virker **ikke** i firefox-esr, og det kan ikke installeres: fbdev-X har ingen KMS/DRI, selvom kernen HAR `pvrsrvkm` loadet — se DOKUMENTATION.md §5.13 og HAANDBOG.md fælde 16. **GPU'en er dog bragt til live (aug 2026):** GLES 3.1 på G6110 via hybris-stakken (vendors system.img + pvrsrvctl + cma=128M) — eget program renderer (DOK §5.15, scripts i `devuan/gpu/`). Browser-WebGL er stadig trin 2 (uger-måneder). Boks 2 flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Se `DOKUMENTATION.md` for den fulde historie, beslutninger og genskabelses-guide (§10 beskriver laptop-flash-metoden og dens fælder), og `TODO.md` for status/viderespor. Scripts i `devuan/`.

**Aktuelt spor:** `devuan/gpu/DDK-HANDOVER-2026-08-26.md` + `devuan/gpu/GPU-FAULT-GENNEMBRUD-SESSION-NOTAT-2026-08-26.md` (DDK-sporet: DDK 1.5@3830101 fundet med GL_EXT_draw_buffers i shader-kompileren — prøveinstallation venter) — opdater denne linje når et nyt spor starter.

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
- **Git-commits:** skriv agent og model i commit-beskeden (fx `[codex:deepseek-v4-flash]` eller den aktuelle model-id), så det altid kan ses hvilken agent/model der lavede ændringen. Notér også agent/modellen i session-notatet hvis den afviger fra den normale.

## Checkpoint- og tråd-disciplin

- Ved milepæle (rodårsag fundet, virkende fix, måling fanget, dokument opdateret) eller når samtalen har været komprimeret 2 gange: opdater et kort session-notat (`devuan/gpu/<EMNE>-SESSION-NOTAT-YYYY-MM-DD.md`) med: hvad vi ved, hvad der er forsøgt, nuværende hypotese, næste skridt og de kommandoer der virker.
- Hold notatet kort og kommando-tungt; læg vigtige fakta i repo-filer (`DOKUMENTATION.md` / `HAANDBOG.md`), ikke kun i samtalehistorikken.
- Hvis du gentager dig selv, spørger om ting du allerede har fastslået, eller genkører kommandoer: sig det højt og foreslå eksplicit at starte en ny tråd, der begynder med "Læs <notatet> og fortsæt derfra".
