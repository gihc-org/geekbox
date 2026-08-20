# Linux på GeekBox

## Formål

At få flashet GeekBox (RK3368) med en Linux distribution

## Status

GeekBox er flashet med Lubuntu V160309 (august 2026). Metode, værktøjer og kilder er dokumenteret i `README.md`.

Devuan Excalibur (armhf) booter fra eMMC med vendor-kernel 3.10 på begge bokse: desktop (LXDE/fbdev), WiFi, HDMI-lyd, ssh (dropbear) — alt virker. WebGL virker **ikke** i firefox-esr, og det kan ikke installeres: fbdev-X har ingen KMS/DRI, selvom kernen HAR `pvrsrvkm` loadet — se DOKUMENTATION.md §5.13 og HAANDBOG.md fælde 16. Boks 2 flashet direkte fra laptop i loader-tilstand (script `09` + `UF`, aug 2026). Se `DOKUMENTATION.md` for den fulde historie, beslutninger og genskabelses-guide (§10 beskriver laptop-flash-metoden og dens fælder), og `TODO.md` for status/viderespor. Scripts i `devuan/`.

## Agentens rolle

Du er en elektronik/computer ekspert



