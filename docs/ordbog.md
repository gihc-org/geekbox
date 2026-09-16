# Ordbogen

## 1. Ordbogen

Læs den her først. Resten af dokumentet bruger ordene uden at forklare dem igen.

**Kerne (kernel).** Linux' inderste del. Den taler direkte med hardwaren. Vores er
producentens egen "vendor-kerne" 3.10 — gammel, men den eneste der har drivere til
chippen. Alt andet på boksen er moderne.

**Driver.** Den del af kernen der styrer et bestemt stykke hardware, fx grafikchippen.

**Rootfs.** Rodfilsystemet: alle filer der udgør systemet — `/etc`, `/usr`, `/home` osv.

**Image.** En fil der indeholder et helt system, klar til at skrives over på boksens
lager. Vores heder `update_devuan.img` og er 1,5 GB.

**Flash.** At skrive et image over på boksens indbyggede lager (eMMC). Boksen skal sættes
i **loader-tilstand** først: hold update-knappen nede mens du tænder den, så lytter den
efter et image gennem USB-kablet i stedet for at boote.

**eMMC.** Boksens indbyggede lager, svarer til en SSD. Cirka 16 GB.

**Bootloader (U-Boot).** Det lille program der kører før kernen og henter den ind i
hukommelsen. Ligger i sin egen del af lageret.

**initramfs.** Et miniature-filsystem som kernen bruger *før* det rigtige rootfs er klar.
Vores er arvet fra 2014-Lubuntu og laver en del rod — se fælde 3.

**PID 1 / init.** Den første proces der starter, og som starter alt andet. Hos os er det
`myinit.sh`, et lille script vi selv har skrevet, som rydder op efter initramfs'en og
derefter overlader arbejdet til det rigtige `init`.

**sysvinit.** Den klassiske måde at starte tjenester på, med scripts i `/etc/init.d/` og
symlinks i `/etc/rc2.d/`. Devuan bruger den — i modsætning til de fleste andre
distributioner, der bruger systemd.

**Framebuffer.** Et stykke hukommelse hvor billedet står, pixel for pixel. 1920 × 1080
punkter i 16-bit farve = cirka 4 MB. Display-controlleren (VOP) læser den 60 gange i
sekundet og sender indholdet ud gennem HDMI. Vil du vide hvad boksen *forsøger* at vise,
kigger du der. Den heder `/dev/fb0`. (VOP og GPU'en er to forskellige ting — se nedenfor.)

**VOP (display-controlleren).** Den del af chippen der læser framebufferen og sender
billedet ud af HDMI'en. Det er VOP'en der viser vores skrivebord — ikke GPU'en.

**GPU (PowerVR G6110).** En separat regneenhed til 3D. Den tegner ingenting af sig selv:
den renderer kun ind i buffere, når et program beder om det gennem hele driver-stakken —
kernel-driver + proprietære blobs + integration mod skærmen. Stakken kører faktisk nu:
blobs'ene er hentet fra dualOS-imaget og kører i deres egen lille Android-hal via
libhybris (docs/grafik/gpu-historien.md §5.15). WebGL virker alligevel ikke — browseren vil kun gennem den
moderne dør (KMS/DRI), som kernen ikke har (fælde 16). Siden 24. aug 2026 findes
der dog en EGL-omvej (`eglplatform_x11`): Firefox' GL-probe er grøn, men hele
browseren blokerer stadig på WebRenders GPU-kontekst — se fælde 23 og
`docs/log/2026-08-24-firefox-webcl.md`.

**KMS/DRM og DRI.** Den moderne vej, grafikprogrammer får billeder på skærmen ad.
Kræver kernens KMS-grænseflade (`/dev/dri`) og en X-driver der bruger den. Vendor-kernen
har ingen af delene — derfor er fbdev den eneste X-driver der findes.

**GLX og EGL.** De to døre, et GL-program (fx firefox' WebGL) kan bruge til at tale med
skærmen. Firefox kræver den ene eller den anden; vores X-server tilbyder kun en tredje,
forældet dør (IGLX), som Firefox ikke bruger.

**X (Xorg).** Programmet der styrer skærm, mus og tastatur. Alle vinduer tegnes af X ned i
framebufferen.

**LXDE, lxpanel, pcmanfm.** Vores skrivebord. `lxpanel` er bjælken i bunden med startmenu
og ur (26 pixels høj). `pcmanfm` tegner skrivebordet og baggrundsbilledet.

**nodm.** Logger automatisk ind og starter skrivebordet, uden login-skærm.

**udev.** Den tjeneste der opdager hardware og giver enhederne navne og mærkater. X spørger
udev om hvilke mus og tastaturer der findes. Virker udev ikke, virker musen ikke.

**ALSA og PulseAudio.** ALSA er kernens lydsystem. PulseAudio (PA) sidder ovenpå og
blander lyd fra flere programmer. Firefox taler med PA, PA taler med ALSA.

**Overscan.** En gammel tradition fra billedrørs-tv: fjernsynet zoomer en lille smule ind
og klipper kanterne af billedet. Moderne tv gør det stadig, når de tror de får et
tv-signal frem for et computer-signal.

**IOMMU / IOVA.** En IOMMU er en oversætter mellem de adresser hardwaren bruger og de
rigtige adresser i hukommelsen. En IOVA er en sådan oversat adresse: den *ser ud* som en
hukommelsesadresse, men peger et helt andet sted hen. Det kostede os en hel dag — se
fælde 8.

---
