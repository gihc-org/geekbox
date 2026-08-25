# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd, ssh (dropbear) — alt virker. WebGL virker **ikke** i firefox-esr, og det kan ikke installeres: fbdev-X har ingen KMS/DRI, selvom kernen HAR `pvrsrvkm` loadet — se DOKUMENTATION.md §5.13 og HAANDBOG.md fælde 16. **GPU'en er dog bragt til live (aug 2026):** GLES 3.1 på G6110 via hybris-stakken (vendors system.img + pvrsrvctl + cma=128M) — eget program renderer (DOK §5.15, scripts i `devuan/gpu/`). Browser-WebGL er stadig trin 2 (uger-måneder). Boks 2 flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Se `DOKUMENTATION.md` for den fulde historie, beslutninger og genskabelses-guide (§10 beskriver laptop-flash-metoden og dens fælder), og `TODO.md` for status/viderespor. Scripts i `devuan/`.

## Agentens rolle

Du er en elektronik/computer ekspert

## Checkpoint- og tråd-disciplin

- Ved milepæle (rodårsag fundet, virkende fix, måling fanget, dokument opdateret) eller når samtalen har været komprimeret 2 gange: opdater et kort session-notat (`devuan/gpu/<EMNE>-SESSION-NOTAT-YYYY-MM-DD.md`) med: hvad vi ved, hvad der er forsøgt, nuværende hypotese, næste skridt og de kommandoer der virker.
- Hold notatet kort og kommando-tungt; læg vigtige fakta i repo-filer (`DOKUMENTATION.md` / `HAANDBOG.md`), ikke kun i samtalehistorikken.
- Hvis du gentager dig selv, spørger om ting du allerede har fastslået, eller genkører kommandoer: sig det højt og foreslå eksplicit at starte en ny tråd, der begynder med "Læs <notatet> og fortsæt derfra".

