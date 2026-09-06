# GeekBox-håndbogen — alt vi har lært, forklaret fra grunden

Denne fil er skrevet så den kan læses uden at kende projektet i forvejen. Den forklarer
hvad boksen er, hvordan man laver en ny, og — vigtigst — hver enkelt fælde vi er faldet i,
så du ikke skal falde i den igen. Skrevet august 2026, efter at syv bokse er blevet
flashet og fejlsøgt.

**Kort om projektet:** en GeekBox er en lille TV-boks fra 2015 med en Rockchip RK3368-chip.
Den blev solgt med Android og senere Lubuntu. Vi har sat et moderne Devuan Linux på den,
men beholdt producentens gamle Linux-kerne (version 3.10 fra 2013), fordi driverne til
grafik, lyd og netværk kun findes til den. Resultatet er en lille skrivebordscomputer der
kan browse, se YouTube og bruges som almindelig maskine.

---

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
libhybris (DOK §5.15). WebGL virker alligevel ikke — browseren vil kun gennem den
moderne dør (KMS/DRI), som kernen ikke har (fælde 16). Siden 24. aug 2026 findes
der dog en EGL-omvej (`eglplatform_x11`): Firefox' GL-probe er grøn, men hele
browseren blokerer stadig på WebRenders GPU-kontekst — se fælde 23 og
`devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md`.

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

## 2. Sådan hænger boksen sammen

```
BootROM (indbygget i chippen, kan ikke slettes)
  → IDB-loader           starter hukommelsen op
  → U-Boot               læser "parameter" og henter kernen
  → parameter            en tekstblok med bl.a. root=/dev/mmcblk0p6 og init=/root/myinit.sh
  → kernel 3.10          starter, initramfs finder rodfilsystemet
  → /root/myinit.sh      VORES script: rydder op, sætter netværk op, udvider disken
  → /sbin/init           sysvinit starter tjenester (netværk, rsyslog, chrony …)
  → nodm                 logger ind som "kristian" og starter X
  → X + LXDE             skrivebordet
```

To detaljer der er værd at kende:

**Imagets rootfs er låst til 1408 MiB.** Vi bygger nyt system ind i det gamle image ved at
overskrive præcis den plads originalen brugte. Det gør flashningen sikker — alle andre dele
af imaget er byte-identiske med producentens — men det betyder at rootfs'en ikke kan blive
større. Med firefox er den fyldt 84 %. `09` har en vagt der stopper i god tid.

**Efter flash udvider `myinit.sh` selv filsystemet** til partitionens 15 GB. Det var
tidligere et manuelt trin, og det blev glemt — med grimme følger (fælde 1).

---

## 3. Sådan laver du en ny boks

Fire trin. Alt andet er automatisk.

```bash
# 1. (kun hvis pakkelisten er ændret) hent pakkerne ind i bygge-rootfs'en
sudo devuan/extra_packages.sh

# 2. byg imaget og flash. Scriptet bygger FØRST (nogle minutter) og venter
#    derefter på ENTER — brug ventetiden på trin 3.
sudo devuan/testflash.sh

# 3. boksen i loader-tilstand: strøm fra → USB-kablet i boksens OTG-port →
#    hold update-knappen → strøm på → slip. Tryk så ENTER i scriptet.
#    Det tjekker selv at boksen er synlig på USB, og siger til hvis den ikke er.

# 4. tag strømmen af og på. Find boksen, og lav swapfilen:
devuan/find_box.sh
ssh -i ~/.ssh/geekbox_key root@<ip> 'bash -s' < devuan/emmc_first_boot.sh
```

Skærmen er sort de første 15-30 sekunder. Det er normalt — der er ikke noget boot-logo.

**Sådan ser en rigtig boot ud:** blåt LXDE-tapet, en grå bjælke i bunden med startmenu til
venstre og ur til højre. Firefox ligger i menuen og kan spille YouTube med lyd.

**Husk USB-donglen til musen.** Sidder den stadig i den forrige boks, ser det ud præcis som
en alvorlig softwarefejl. Vi faldt i.

---

## 4. Fælderne

Hver fælde står som: hvad du ser → hvad der egentlig sker → hvordan du afgør det →
hvad der er gjort ved det.

### Fælde 1: Fuld disk — skærmen bliver sort, og alle spor forsvinder

**Du ser:** boksen booter, men skrivebordet kommer aldrig. Sort skærm. Ingen fejlbesked
nogen steder. `.xsession-errors` er tom. Loggene er tomme.

**Hvad der sker:** imagets filsystem er kun 1,4 GB. Browser-cache kan fylde det på en
aften. Når disken er helt fuld, kan intet program skrive — heller ikke fejlmeddelelser.
X-sessionen dør stille, og fordi ingen kan skrive noget ned, ser det ud som en helt anden
fejl. Vi brugte over en time på at lede i lyddriveren, fordi det var det vi havde rørt
sidst.

**Sådan afgør du det:** `df -h /`. Tre sekunder. Står der 100 %, er det den.

**Gjort:** `myinit.sh` udvider nu selv filsystemet til hele partitionen ved hver boot, hvis
det mangler mere end 5 %. Verificeret på en frisk flash: `/root/resize.log` viser
"now 3799552 (4k) blocks long", og `df` viser 15 GB.

**Læren:** `df` hører i de første tre kommandoer du kører, når noget uforklarligt går i
stå. En fuld disk sletter sine egne beviser.

### Fælde 2: To udevd'er — mus, tastatur og lyd dør samtidig, tavst

**Du ser:** musen reagerer ikke. Tastaturet heller ikke. Og der er ingen lyd. Tre
symptomer, som ser ud som tre problemer.

**Hvad der sker:** den gamle initramfs starter sin egen `udevd`, og den overlever ind i
vores system. Bagefter starter `rcS` endnu en. To daemoner slås om den samme
kommunikationskanal til kernen, og resultatet er at **udev-databasen aldrig bliver
skrevet**. Så:

- X spørger udev om input-enheder og får ingenting. `Xorg.0.log` skriver kun
  *"The server relies on udev to provide the list of input devices"* og tilføjer aldrig
  noget. Ingen fejl. Mus død.
- PulseAudio kan ikke finde lydkortet og laver i stedet en "null-sink" — en spand lyden
  hældes i og forsvinder. Alt spiller lydløst.

**Sådan afgør du det:** `pgrep -a udevd` — er der mere end én, er det den. Og
`ls /run/udev/data | wc -l` skal give omkring 200, ikke 0.

**Gjort:** `myinit.sh` dræber initramfs' udevd før `init` starter, så `rcS` starter præcis
én. Verificeret: én daemon, 199-206 poster i databasen, X tilføjer 6-7 input-enheder.

### Fælde 3: Lyd kræver et gammelt bibliotek — og det skal sidde det rigtige sted

**Du ser:** ingen lyd i firefox, selvom lydindstillingerne ser rigtige ud.

**Hvad der sker:** to lag skal passe sammen. Kernen er fra 2013 og kender ikke de
funktionskald som det moderne ALSA-bibliotek bruger. Derfor har vi en ældre udgave af
biblioteket liggende i `/opt/alsa-da`, og PulseAudio skal startes med en sti der peger på
den. Gør den ikke det, kan PA ikke åbne lydkortet og laver en null-sink.

**Stien skal sidde på pulseaudios EGEN startlinje** i
`/etc/xdg/autostart/pulseaudio.desktop`:

```
Exec=env LD_LIBRARY_PATH=/opt/alsa-da/usr/lib/arm-linux-gnueabihf start-pulseaudio-x11
```

To andre steder virker **ikke**, og vi prøvede dem begge:

- `/etc/profile.d/` læses kun når man logger ind i en terminal. Skrivebordet ser den ikke.
- `/etc/environment` læses ved login gennem PAM — men nodms session får den ikke.

Og et tredje sted er direkte **farligt**: en fil i `/etc/X11/Xsession.d/` tvinger det gamle
bibliotek ned over hele sessionen. Biblioteket mangler funktioner som nyere programmer
kræver (`aplay` fejler fx med "undefined symbol: snd_pcm_subformat_value"), og sessionen
døde af det.

**Sådan afgør du det:**

```bash
pactl list sinks short
#   alsa_output.dmixer  = rigtigt
#   auto_null           = lyden går i ingenting
grep -c alsa-da /proc/$(pgrep -x pulseaudio | head -1)/maps    # skal være over 0
```

**Gjort:** både `07` og `09` sætter linjen. Test lyden med PulseAudio, ikke med `aplay` —
`aplay` kan ikke køre mod det gamle bibliotek og giver et misvisende svar.

### Fælde 4: Fjernsynet klipper kanterne af billedet

**Du ser:** skrivebordet er der, men bjælken i bunden mangler — eller du kan se 2 pixels
af den. Startmenu-ikonet i venstre hjørne er også væk.

**Hvad der sker:** fjernsynet viser ikke hele billedet. Det klipper cirka 2,3 % af på alle
fire kanter: omkring 25 pixel-linjer i toppen, 25 i bunden og 48 i hver side. Bjælken er 26
pixels høj og ligger yderst nede, så den forsvinder helt. Boksen sender hele tiden et
korrekt billede — det er skærmen der ikke viser det.

Værre endnu: **det sker ikke hver gang.** Fire boots i træk gav: klippet, fint, klippet,
fint. Fjernsynet beslutter sig ud fra HDMI-forhandlingen ved opstart. Det var derfor
symptomet virkede som "første boot er altid gal, næste er fin", og det sendte os på jagt
efter en opstartsfejl der ikke fandtes.

**Sådan afgør du det:** tegn farvede bjælker i framebufferen i kendte højder og se hvilke
du kan se. Er kanterne væk i både top og bund, og det symmetrisk, er det fjernsynet — en
fejl i boksens timing ville forskyde billedet, ikke klippe lige meget af i begge ender.

**Gjort:** `fb_overscan.py` skrumper billedet til 95 % og centrerer det, så fjernsynets
beskæring kun spiser en sort kant. Den kører automatisk ved hver login.

Den *pænere* løsning er fjernsynets egen menu (Samsung: Billede → Billedstørrelse →
Skærmtilpasning, eller omdøb HDMI-indgangen til "PC"), for så bliver billedet skarpt i
kanterne. Men så er man afhængig af en tv-indstilling, og vi ville have noget der virker
uanset hvad man sætter boksen til.

Kernens egen kompensation kan i øvrigt ikke bruges: funktionen er **død kode** — den giver
op på anden linje når skærmen sidder i HDMI, hvad den altid gør på en tv-boks. Producenten
har efterladt en halvfærdig funktion med et bandeord i fejlbeskeden.

### Fælde 5: Sort skrivebord er ikke det samme som en død skærm

**Du ser:** sort skærm med kun en musemarkør.

**Hvad der sker:** skrivebordsprogrammet henter baggrundsbilledet gennem en genvej,
`/etc/alternatives/desktop-background`, som laves af en pakke vi ikke installerer. Uden den
tegner det bare sort. Kombineret med fælde 4 (bjælken klippet væk) er hele skærmen sort, og
det ser ud som om grafikken slet ikke virker.

**Sådan afgør du det:** er baggrunden blå, virker grafikken. Er den sort, mangler genvejen.

**Gjort:** både `07` og `09` laver genvejen til LXDE's eget tapet. Et blåt tapet er
desuden det bedste diagnoseværktøj vi har: det gør forskellen mellem "grafikken er død" og
"noget andet er i vejen" synlig på et halvt sekund.

### Fælde 6: `sudo` var i gruppen, men pakken var ikke installeret

**Du ser:** `sudo ls` svarer "kommandoen ikke fundet".

**Hvad der sker:** brugeren stod i `sudo`-*gruppen*, men programmet var aldrig installeret.
Gruppen alene gør ingenting.

**Gjort:** `sudo` er i pakkelisten, og `09` stopper bygningen hvis den mangler.
Adgangskoden er `geekbox` fra opsætningen — **skift den** med `passwd`.

### Fælde 7: Der var ingen syslog — vi fejlsøgte i blinde

**Du ser:** ingenting. Det er problemet.

**Hvad der sker:** der kørte ingen syslog-tjeneste på boksen, så nodms og andre tjenesters
fejlbeskeder blev aldrig skrevet ned. Både den fulde disk og den døde udev var helt tavse.
Havde loggen været der, havde vi set begge ting med det samme.

**Gjort:** `rsyslog` er i pakkelisten (bemærk: `sysklogd` findes ikke længere i denne
Devuan-udgave) og starter i runlevel 2. Nu er der en `/var/log/syslog` at læse i.

### Fælde 8: `/dev/mem` rammer ikke framebufferen — dagens dyreste fejl

**Du ser:** en overbevisende men helt forkert teori.

**Hvad der sker:** man kan læse computerens hukommelse gennem `/dev/mem`, og filen
`/sys/class/graphics/fb0/phys_addr` fortæller hvor framebufferen ligger. Men grafikchippen
bruger en IOMMU, så det tal er en **IOVA** — en oversat adresse, der peger et andet sted
hen end den ser ud til. Alle vores målinger gik derfor ned i tilfældig hukommelse, ikke i
billedet. Vi byggede en hel teori om at billedet blev afkortet, og der var aldrig nogen
afkortning.

**Beviset var to linjer.** Vi skrev det samme mønster gennem den rigtige vej og læste det
gennem den forkerte:

```
via mmap på /dev/fb0 : 3412341234123412
via /dev/mem         : 0000000004000000
```

**Sådan gør du i stedet:** `mmap` på `/dev/fb0`. Så går man gennem kernen, som selv finder
den rigtige hukommelse.

```python
import mmap, os
m = mmap.mmap(os.open("/dev/fb0", os.O_RDWR), 3840*1080)
m[y*3840:(y+1)*3840] = b"\xe0\x07"*1920     # en grøn linje i højde y
```

En lille detalje der også narrede os: `m.flush()` giver fejlen EINVAL på en enhedsfil. Det
er harmløst, og skrivningen er allerede sket.

### Fælde 9: At måle sit eget testmiljø

**Du ser:** en rettelse der "virker", men ikke gør det.

**Hvad der sker:** vi kontrollerede lyden med `su kristian -c pactl`. Den kommando startede
selv en ny PulseAudio, og `su` læser `/etc/environment` — så *den* daemon fik biblioteksstien
og lavede en rigtig lydkanal. Skrivebordets egen PulseAudio havde den ikke. Vi målte altså
vores eget testmiljø og meldte "lyden virker" mens brugeren sad uden lyd.

**Sådan gør du i stedet:** mål altid på den proces sessionen selv har startet:

```bash
grep -c alsa-da /proc/$(pgrep -x pulseaudio | head -1)/maps
```

### Fælde 10: En rettelse i et script der ikke bliver kørt igen, er ingen rettelse

**Du ser:** en fejl du er sikker på du har rettet, som stadig er der efter en ny flash.

**Hvad der sker:** `07` kører man én gang når rootfs'en bygges. `09` kører man hver gang før
en flash. Retter man noget i `07` uden at køre det igen, kommer rettelsen aldrig med. Det
skete tre gange på én aften: tapet-genvejen, `video`-gruppen og pulseaudios startlinje.

**Gjort:** alt hvad en boks ikke kan undvære, har nu et sikkerhedsnet i `09` med en vagt der
stopper bygningen hvis det mangler. Regel fremover: **hvis en boks ikke kan undvære det,
skal `09` sikre det.**

### Fælde 11: Wifi kan ses men ikke tilsluttes — polkit mangler en session

**Du ser:** netværks-ikonet er i bjælken, wifi-netværkene står på listen, men når du vælger
et, sker der ingenting — eller du bliver spurgt om en adgangskode der ikke bliver godtaget.

**Hvad der sker:** polkit er det system der afgør om en bruger må ændre systemindstillinger.
Dets standardregler kræver at brugeren har en "aktiv session", og den slags session laves
normalt af logind. Vores `nodm` logger ind uden at lave en, så polkit kan ikke se nogen
aktiv bruger og siger nej til alt.

**Løsningen** er en regel der giver ja ud fra **gruppemedlemskab** i stedet — brugeren skal
være i gruppen `netdev`, og en fil i `/etc/polkit-1/rules.d/` giver den gruppe lov:

```javascript
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.isInGroup("netdev")) {
        return polkit.Result.YES;
    }
});
```

**Gjort:** `09` sætter både gruppen og reglen, hvis NetworkManager er i rootfs'en. Bemærk
også at `wlan0` **ikke** må stå i `/etc/network/interfaces` — gør den det, lader NM den
være (Debians `[ifupdown] managed=false`). `eth0` bliver derimod med vilje i den fil, så
den tidlige netværksopsætning og ssh-adgangen er uændret.

### Fælde 12: Firefox dør uden swap — men boksen bliver ved at køre

**Du ser:** firefox lukker ned af sig selv, eller et faneblad bliver til en fejlside. Resten
af maskinen kører videre: du kan pinge, browse i en ny fane, bruge terminalen.

**Hvad der sker:** boksen har 2 GB RAM. Firefox med YouTube bruger let 500-800 MB, og når
hukommelsen er brugt op og der ikke er nogen swapfil at falde tilbage på, mislykkes en
hukommelsesanmodning. Firefox opdager det selv og lukker den ramte proces ned — derfor
"fanebladet gik ned" frem for at hele maskinen frøs. Der står **intet** i kernens log om
det, fordi det ikke er kernens OOM-dræber der har været i gang: der er ingen
"Killed process"-linjer at finde.

Beviset ligger i stedet hos firefox selv: en fil under
`~/.mozilla/firefox/*/minidumps/*.dmp` med tidsstempel fra nedbruddet.

**Sådan afgør du det:** `swapon --show`. Er den tom, er der ingen swap. `free -h` viser om
hukommelsen er ved at være brugt op.

**Gjort:** `myinit.sh` laver nu selv en 2 GB swapfil ved første boot, hvis der ikke er
nogen. Det tager 1-2 minutter én gang, og det sker efter at ssh er startet, så man kan
komme ind imens. Fælde-mønsteret var det samme som med `resize2fs`: et manuelt efter-trin
der bliver glemt.

**To fælder i fælden**, begge fundet ved at implementeringen fejlede i praksis:

*Test ikke om der er swap med `[ -s /proc/swaps ]`.* Procfs rapporterer altid størrelse 0,
så testen er altid sand. Kig på indholdet i stedet: `grep -q "^/" /proc/swaps`.

*En halv swapfil må aldrig blive stående.* Filen laves ved første boot, og det er præcis
dér man tager strømmen — fordi man tror boksen er færdig. Så står der en ufuldstændig fil
uden swap-signatur, og tjekker koden kun **om filen findes**, springer næste boot
oprettelsen over og `swapon` fejler tavst for evigt. Det skete: en 136 MiB rest, og en boks
der troede den havde swap. Rettelsen er at sammenligne **størrelsen** med den ønskede, og
at bygge i en `.tmp`-fil der først omdøbes når `mkswap` er lykkedes. Så er en afbrudt boot
harmløs.

### Fælde 13: Logningen æder eMMC'en, når den endelig virker

**Du ser:** `/var/log/syslog` og `/var/log/kern.log` vokser med 15 MB i timen hver, og
begge er fyldt med registerdumps.

**Hvad der sker:** kernen er fra 2013 og kender ikke systemkaldet `clock_gettime64`, som
alle moderne programmer bruger. Hver gang det kaldes, skriver kernen et **komplet
registerdump** i loggen. Det er harmløst i sig selv — men da vi installerede rsyslog for at
kunne fejlsøge, begyndte alt det at blive skrevet til eMMC'en: cirka 30 MB i timen,
700 MB om dagen, oveni at det drukner alle rigtige beskeder.

Dumpet er markeret KERN_WARNING, så man kan ikke filtrere det væk på prioritet uden også at
miste rigtige advarsler. Løsningen er et filter på de linjeformer dumpet består af:
`syscall 403`, `do_ni_syscall`, `PC is at`, `LR is at`, `Code:`, registerlinjerne (`x0 :`,
`pc :`, `sp :`), `task:` og `CPU: n PID: n Comm:`.

**En detalje der kostede en runde:** rsyslogs `:msg, regex,` bruger POSIX **BRE**, hvor `+`
er et almindeligt tegn og `(a|b)` ikke betyder noget. Skriv `:msg, ereregex,` i stedet.

**Gjort:** `09` lægger filteret i `/etc/rsyslog.d/`. Målt effekt: fra ~950 linjer pr. 45
sekunder til **0**, mens `logger` stadig kommer igennem, og rigtige oops-linjer (`BUG:`,
`Internal error`) ikke rammes af filteret.

### Fælde 14: Firefox afbrydes med "stack smashing" — og sporet er lukket

**Du ser:** firefox lukker af sig selv, eller et faneblad bliver til en fejlside, efter
nogle minutter på YouTube. Det sker også på andre tunge sider, fx dr.dk. Det ser ud som
hukommelsesmangel, men er det ikke.

**Hvad der sker:** i firefox' udskrift står linjen

```
*** stack smashing detected ***: terminated
```

Det er glibcs stak-beskyttelse. Hver funktion lægger en kontrolværdi ("canary") på stakken,
og opdager glibc at den er overskrevet, afbryder den programmet med `SIGABRT` frem for at
lade det køre videre med ødelagt hukommelse. Kernen bekræfter det med *"potentially
unexpected fatal signal 6"* (6 = SIGABRT), og angiver hvilken proces det var —
hos os `Comm: Isolated Web Co`, altså en sandkasse-isoleret indholdsproces.

Det er altså **ægte hukommelseskorruption**, ikke pladsmangel. Målt samtidig: 1 GiB fri
hukommelse, og swappen aldrig rørt. Så mere RAM eller mere swap havde ikke hjulpet.

**Hvorfor vi ikke fandt stedet.** Begge veje til et stakspor er lukkede på denne boks:

- **Kernedumps findes ikke.** `/proc/sys/kernel/core_pattern` eksisterer ikke — vendor-kernen
  er bygget uden `CONFIG_COREDUMP`. Man kan altså ikke få processens hukommelse på disken
  og læse den med `gdb` bagefter.
- **Firefox' egen nedbrudsrapport fejler.** Loggen siger `ExceptionHandler::GenerateDump
  minidump generation failed`, og minidumps-mappen er tom. Derfor ingen signatur, ingen
  funktionsnavne.
- `gdb` kan hænges på hovedprocessen, men nedbruddene rammer mest indholdsprocesserne, som
  starter og stopper hele tiden.

**Arbejdshypotesen** (læs: ikke bevist): firefox' sandkasse bruger seccomp til at fange
systemkald, og **emulerer** dem den ikke lader passere. Vores kerne mangler
`clock_gettime64`, som moderne biblioteker kalder konstant — vi har målt tusindvis i
minuttet. Hvert kald går gennem sandkassens signalhåndtering, der kører på en separat stak.
Går noget skævt dér, ser resultatet ud præcis som det målte: ødelagt stak i tilfældige
processer, mest de sandkasse-isolerede.

**Gjort** — fire indstillinger i `/etc/firefox-esr/firefox-esr.js`, som `09` lægger ind:

```javascript
pref("security.sandbox.content.level", 0);   // hovedmistænkte
pref("fission.autostart", false);            // ingen proces per website
pref("dom.ipc.processCount", 1);
pref("browser.sessionstore.resume_from_crash", true);
```

Efter det kørte YouTube syv minutter uden nedbrud, hvor den før døde inden for få minutter,
og procestypen der crashede (`isolatedWebContent`) findes slet ikke længere.

**Det ærlige forbehold:** fire indstillinger blev ændret på én gang, så vi ved **ikke**
hvilken der var afgørende. Vil man vide det, findes der én test: sæt
`security.sandbox.content.level` tilbage til `2` og lad resten være. Vender nedbruddene
tilbage, er sandkassen synderen — og så kan site-isolation tændes igen, som er en reel
sikkerhedsfunktion man ellers giver væk.

**To ting der er værd at bruge næste gang noget crasher uforklarligt:**

```bash
echo 1 > /proc/sys/debug/exception-trace      # kernen logger uhåndterede signaler
echo 1 > /proc/sys/kernel/print-fatal-signals # med procesnavn og registre
```

Begge nulstilles ved boot. Uden dem er et nedbrud i en almindelig proces helt tavst i
kernens log, og man tror fejlagtigt at "der står ingenting nogen steder".

### Fælde 15: Småting der koster timer

- **`uboot-logo-on = 0` i DTB'en gør at boksen ikke booter.** Lysdioden bliver lilla og
  aldrig blå. Flaget styrer også bootloaderens egen skærmopsætning, i kode vi ikke har.
  Rollback: `devuan/rollback.sh`.
- **Et selv-pakket bootimg uden korrekt `id`-felt fryser ved lilla LED** — kernen når
  aldrig i gang. Rockchip-U-Boot verificerer `id` (offset 0x240, 20 bytes) med
  SHA1 over kernel/ramdisk/second + størrelser + resten af headeren
  (`SecureVerify.c`). Symptomet er identisk med andre lilla-frysere, men kernens
  indhold kan være helt fint. Brug altid
  `devuan/gpu/kernelbuild/package_bootimg.py` til at pakke bootimg'er — den
  beregner id'et (verificeret mod originalen 26. aug 2026).
- **`reboot` slukker boksen** i stedet for at genstarte. Brug strømcyklus.
- **nodm holder op med at prøve** hvis sessionen dør flere gange hurtigt efter hinanden
  (`NODM_MIN_SESSION_TIME=60`). Genstart derfor ikke X to gange inden for et minut — så
  kommer skrivebordet ikke igen før næste strømcyklus.
- **`/proc/mounts` har to poster for `/`**: først initramfs' egen, derefter den rigtige
  `/dev/mmcblk0p6`. Tager man den første, får man noget der ikke er en disk.
- **lxpanel indlæser hver fil i `panels/`-mappen.** En backup med navnet `panel.bak` dér
  giver et helt ekstra panel på skærmen. Læg backups uden for mappen.
- **Boksen har ingen MAC-adresse i hardwaren**, så kernen laver en tilfældig ved hver boot.
  Derfor får boksen en ny IP hver gang, og derfor kan man ikke lede efter den på MAC.
  `devuan/find_box.sh` leder efter dropbear-svar i stedet.
- **`cat /dev/fb0` viser kun den øverste halvdel** af skærmen. Brug `mmap`.
- **`y_vir=960` i driverens statusfil er ikke en højde.** Det er linjelængden målt i
  4-byte-ord: 1920 × 2 ÷ 4 = 960. Helt normalt.

### Fælde 16: WebGL virker ikke — og det kan ikke installeres

**OPDATERET (26. aug 2026):** dette er nu LØST — DDK 1.5@3830101 kører (genbygget
3.10-kernel + 1.5-KM `.ko` + 1.5-UM), `shader_ext_test` accepterer
GL_EXT_draw_buffers (ES2+ES3), og Firefox WebGL virker (GL_VERSION "OpenGL ES 3.1
build 1.5@3830101"). Resten af fælden er den historiske baggrund. Se §5.15 +
fælde 26-31 for de nye fælder.

**Du ser:** et webspil melder "browseren understøtter ikke WebGL". Firefox er ny
(140-esr) og har ikke slået WebGL fra i indstillingerne. Alligevel nægter den.

**Hvad der sker:** WebGL kræver en dør ind i grafikken — og browseren vil kun bruge
den moderne dør, som vores opsætning ikke har. En GPU-driver er tre lag, og alle tre
er nu på boksen:

1. **Kerne-driveren** (`pvrsrvkm`) er loadet — `/dev/pvrsrvkm` findes. Men den er bare
   døren: den tager imod kommandoer, den udfører ingenting selv.
2. **De proprietære blobs** — laget der faktisk forstår 3D-kommandoer — er lukkede,
   Android-byggede fra 2016. Vi hentede dem fra dualOS-imagets `system.img` og kører
   dem gennem libhybris i deres egen lille Android-hal (DOK §5.15).
3. **Integrationen mod skærmen** — den gamle vej (libhybris) virker nu for vores egne
   programmer, men den moderne vej (KMS/DRI) mangler stadig i vendor-kernen — og det
   er kun den vej, browseren accepterer.

Værre: selv **software-GL** (CPU-rendering, som ellers redder maskiner uden GPU) er
spærret her. Det skal nemlig også gennem DRI, og vores X-server (fbdev) har ingen DRI.
X-serverens log siger det lige ud: *"Screen 0 is not DRI2 capable"*. Det eneste den
byder på, er en forældet nødløsning (IGLX), som Firefox ikke bruger.

**Derfor var fbdev ikke rigtig et valg.** Boksen har to grafikdele: VOP'en sender
billedet ud af HDMI, og GPU'en renderer 3D. Uden KMS i kernen er den eneste X-driver
den der tegner i hukommelsen med CPU'en — fbdev. Producenten gjorde præcis det samme i
deres egen Lubuntu.

**Sådan afgør du det:** fire linjer, alle skal ramme:

```bash
ls /sys/class/drm                          # tomt = ingen KMS
grep AIGLX /var/log/Xorg.0.log             # "Screen 0 is not DRI2 capable"
ls /dev/pvrsrvkm                           # findes = kerne-driveren er der
dpkg -l | grep mesa                        # installeret = bibliotekerne fejler ikke noget
```

**Gjort:** hele GPU-stakken blev bragt op bagefter — blobs'ene kom ind, og fabrikken
tegner (DOK §5.15). Men WebGL i Firefox kan stadig ikke lade sig gøre: browseren
kræver KMS/DRI i kernen. Hvis WebGL-spil er et mål, er `chromium` vejen: den har sin
egen software-GL (SwiftShader) indbygget og behøver ingen system-GL. Den findes til
armhf i arkivet, men forvent `--no-sandbox` (samme syscall-problemer som fælde 14) og
lav fart — alt renderes på CPU'en.

**Opdatering (24. aug 2026):** med `eglplatform_x11` (hybris-EGL → X-vindue) har
Firefox fået en EGL-vej udenom KMS/DRI — dens GL-probe (`glxtest`) er nu GRØN
(PowerVR Rogue G6110, GLES 3.1, TEST_TYPE=EGL; fælde 23 er løst). Hele Firefox
blokerer stadig på WebRenders GPU-kontekst (to målte fejlmønstre, 0x300c/0x3000),
så WebGL i browseren virker fortsat ikke i dag — status og næste skridt:
`devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md`.

### Fælde 17: Boksen dør brat under belastning — strømforsyningen løj om 2A

**Du ser:** boksen genstarter sig selv midt i tunge opgaver — første gang under
`apt-get install chromium`, siden under en bevidst stress-test. Ingen fejlbesked nogen
steder, hverken i syslog eller kern.log. Efter nedbruddet booter den fint igen.

**Hvad der sker:** strømforsyningen kan ikke levere det, mærkaten lover ("5V 2A"). Når
CPU'en og eMMC'en arbejder samtidig, dykker spændingen, og PMIC'en (rk808) resetter
SoC'en — **uden at strømmen tages helt**. Derfor er der hverken panik eller OOM i
loggene: kernen nåede aldrig at fejle noget.

Beviskæden (alt målt aug 2026):

- **Logfilerne ender i NUL-byte-blokke.** De var vokset i størrelse, men data nåede
  aldrig ud af page-cachen. Klassisk hård død midt i skrivning.
- **Uret beholdt tiden.** Ved hver boot efter et nedbrud stod kernel-uret allerede på
  2026 — RTC'en (i PMIC'en) havde aldrig mistet strømmen. Havde nogen trukket stikket,
  var uret startet i 2013.
- **A/B-forsøget:** `devuan/stress_test.sh` dræbte boksen på 2A-adapteren (to gange)
  og blev overlevet på en 2,4A-lader (fuld test, 4½ minut med load ~10 på 8 kerner).

**Sådan afgør du det:** kør `devuan/stress_test.sh` på boksen (udpakker de cachede
.deb'er tre gange + 8 travle CPU'er + 600 MB disk-skrivning; overvågningsloggen havner
i `/root/stress_mon.log`). Dør boksen, skift strømforsyning og kør igen — overlever den
på den nye, er adapteren dømt. Antallet af genstarter ses med
`grep -ac "Linux version" /var/log/kern.log`.

**Gjort:** boksen kører på 2,4A-laderen. Mærk 2A-adapteren, så den ikke havner på en
boks igen. Et multimeter på 5V-stikket under belastning ville sætte sidste punktum
(spændingen skal holde sig over ~4,75V), men A/B-forsøget er allerede overbevisende.

**Fælde i fælden:** nedbruddet efterlod dpkg i en brudt tilstand (pakken halvt
udpakket, 23 pakker ukonfigurerede). Reparation — og rækkefølgen betyder noget, for
`apt-get -f install` kan ikke konfigurere en pakke, hvis kontrolfiler mangler:

```bash
dpkg --remove --force-remove-reinstreq chromium
apt-get -f install          # konfigurerer resten
apt-get --purge autoremove  # rydder de pakker chromium trak med
```

---

### Fælde 18: Daemonen dør med kode 42 — et tomt hul i hybris' funktionstabel

**Symptom:** `gles_daemon` svarer på `ping`/`fb`, men dør på den første `render`.
Klienten får BrokenPipe; loggen slutter lige efter den sidste kommando; `dmesg` er
tavs. Lige før døden kører processen "display-dansen" (`[system-shim]`-linjer med
`chvt 7` og `/sys/class/display/*/enable`-toggle) og afslutter med `exit(42)` —
den dør altså IKKE af et signal, den lukker sig selv pænt.

**Årsag:** hybris' `libGLESv2.so.2` (wrapperen) kalder den ægte GLES-funktion
gennem en slot i sin egen hukommelse (BSS, offset 0x101dc). For `glReadPixels`
var slottet aldrig udfyldt (init'ens `android_dlsym` løste symbolet ikke) →
kaldet går gennem NULL → SIGSEGV. machybrisegl's signal-fælde
(`catch_exit_signals` i libEGL) fanger det, kører `cleanup()` (= display-dansen)
og kalder `exit(42)` — derfor ser det ud som en bevidst nedlukning, ikke et
nedbrud.

**Kur:** `patch_readpixels()` i `devuan/gpu/gles_daemon.c` — efter EGL-init:
`hybris_dlopen("libGLESv2.so")` + `hybris_dlsym("glReadPixels")` (den ægte
funktion ligger i `/system/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so` og ER
eksporteret), og skriv pointeren ind i slottet (`base + 0x101dc`). Derefter
virker readback fra både FBO og default-framebuffer.

**Detektiv-sporet:** strace (dansen + `exit_group(42)`), grep "SIG" i strace
(`SIGSEGV si_addr=NULL`), gdb (`glReadPixels_wrapper` kaldte 0x0), disassembly af
wrapperen (slottet). Alt dokumenteret i DOK §5.15a.

### Fælde 19: Hybris' EGL-init skifter aktiv VT — X tegner ikke til fb0, før man skifter tilbage

**Symptom:** Et GLES-program opretter et X-vindue, `xwininfo` siger
`Map State: IsViewable` og XPutImage køres uden fejl — men `/dev/fb0` er urørt
(fbdump viser skrivebordet i vindue-området). Samme program virker pludselig, når
man manuelt kører `chvt <X' vt>`.

**Årsag:** Hybris' EGL-init (machybrisegl) kører en "display-dans" ved opstart og
efterlader den AKTIVE VT på en anden kanal end X' (målt: vt10; X lå på vt7/vt8).
fbdev-X' shadow-framebuffer kopieres kun til fb0, når X' egen VT er aktiv — er den
ikke, tegner X ind i skyggen, men intet når lærredet. Alle tegninger ser derfor ud
til at forsvinde, selvom vinduet og requestene er perfekte.

**Kur:** Skift tilbage til X' VT EFTER EGL-init (og helst ved første present):
find Xorgs VT via `/proc/<pid>/cmdline` og kør `chvt <vt>` / ioctl `VT_ACTIVATE` +
`VT_WAITACTIVE` på `/dev/tty0`. `eglplatform_x11` gør det selv i `ensure_x_vt()`.

**Detektiv-sporet:** `fgconsole` viste vt10 under kørslerne; `chvt 8` + gentest
genoprettede rendering (6.420 px forskel i fbdump). Alt andet (LD_PRELOAD,
forbindelse, XImage) var vildspor — "mål VT først".

### Fælde 20: Tegning fra en anden X-forbindelse når ikke fb0

**Symptom:** XPutImage/XFillRectangle uden X-fejl (XSync passerer), vinduet er
IsViewable — men intet vises, heller ikke efter fælde 19-fixet. Tegning fra
vinduets egen forbindelse virker derimod.

**Årsag:** På denne boks' fbdev-X-server renderer tegninger kun, når de kommer fra
den X-forbindelse, der oprettede vinduet. En "platform", der åbner sin egen
forbindelse (`XOpenDisplay` i init_module) og tegner derfra, afleverer sine
requests til serveren — men de når aldrig lærredet (serveren accepterer dem uden
fejl).

**Kur:** Tegn gennem vinduets egen forbindelse. I `eglplatform_x11` sender klienten
sit `Display*` som EGL-native-display (`eglGetDisplay((EGLNativeDisplayType)dpy)`),
og platformens `GetDisplay` gemmer det til `present()`.

**Detektiv-sporet:** xdraw-probe med `--secondconn` (0 px) vs. samme forbindelse
(6.420 px); XGetImage læste sort tilbage; request-strømme (strace `writev`) var
byte-identiske i begge tilfælde.

### Fælde 21: `popen`/`pgrep`/`system()` fejler i hybris-processer — ødelagt environ

**Symptom:** Et hybris-loadet program (fx `eglplatform_x11` eller `gles_daemon`)
kalder `system()`, `popen()` eller lignende for at køre et hjælpeprogram — og det
virker ikke (execve fejler med EFAULT, `popen` returnerer NULL uden forklaring).

**Årsag:** Efter hybris-init er processens `environ` ødelagt (målt i projektet:
glibc 2.41's `system()` på 3.10-kernen). Det rammer alt, der exec'er: `system()`,
`popen()`, og indirekte kommandoer der bygger på dem.

**Kur:** `system_shim.so` overtager `system()` (kører med RENT env). For alt andet:
undgå fork/exec — læs `/proc` direkte (`opendir`/`readdir` + `read` af `cmdline`)
og brug ioctl i stedet for `chvt`-kommandoen. Målt: `popen` fejler, direkte
`/proc`-scanning virker.

### Fælde 22: `/proc/<pid>/cmdline` har NUL-separerede argumenter — almindelig `strstr` stopper for tidligt

**Symptom:** Man scanner `/proc/*/cmdline` efter fx "vt8" hos Xorg — og finder det
ikke, selvom `cat` viser det. F.eks. fælde 19-fixet "virker ikke", når det bygges
med `strstr(cmd, "vt")`.

**Årsag:** `/proc/<pid>/cmdline` adskiller argumenterne med NUL-byte. `strstr`
stopper ved den første NUL (efter argv[0]=".../Xorg") og når aldrig "vt8".

**Kur:** Iterér argument-for-argument: `for (p = cmd; p < cmd + n; p += strlen(p)+1)`
og sammenlign hvert argument med `strncmp`/`strcmp`.

### Fælde 23: Firefox' EGL-probe rammer Android-loaderens `eglGetDisplay` — EGL_BAD_DISPLAY

**Symptom:** firefox-esr med `MOZ_X11_EGL=1` + vores env melder
`glxtest: libEGL no display` og renderer med Mesa/llvmpipe, selvom
`eglplatform_x11` virker for egne programmer. logd viser
`eglGetDisplay:218 error 300c` (EGL_BAD_DISPLAY), og strace viser at glxtest
loader `/system/lib/libEGL.so` + `/vendor/lib/egl/libEGL_POWERVR_ROGUE.so`.

**Årsag:** Firefox' GL-probe (`glxtest`) dlopen'er vores `libEGL.so.1`, og
hybris/bionic loader Android-EGL-kæden ind. I glxtest's proces rammer
`eglGetDisplay` Android-loaderens version (som kun forstår Android-skærme) —
med et ikke-default display (fx X-`Display*`) svarer den EGL_BAD_DISPLAY.
Vores hybris-wrapper (der kender `eglplatform_x11`) bliver ikke ramt, selvom
de samme kald virker i `dlopen_egl_test.cpp`/`egl_display_probe.cpp`.
Præcisering (målt 24. aug 2026): glxtest henter **alle** kerne-EGL-funktioner
gennem `eglGetProcAddress("eglGetDisplay")` (se `get_egl_status` i
`toolkit/xre/glxtest/glxtest.cpp`) — IKKE via dlsym. Wrapperens
`eglGetProcAddress`-kæde er: special-cases → dlsym(platform) →
`ws_eglGetProcAddress` (platformens egen) → Android-loaderens interne
funktioner. Vores platforms `ws_eglGetProcAddress` returnerede NULL for
kerne-EGL-navne (delegere til `eglplatformcommon`), så Android-intern
`eglGetDisplay` (+0x50c0 i `/system/lib/libEGL.so`, kun r0==0 accepteres)
vandt. De samme kald via direkte dlsym gav altid wrapperens version — derfor
var de "samme kald virker i kloner"-prober et vildspor: de testede dlsym-vejen.

**Kur (løst 24. aug 2026):** platformens `ws_eglGetProcAddress`
(`eglplatform_x11.cpp`) videresender nu kerne-EGL-navne til wrapperens egne
eksporter (`dlopen("/opt/hybris/libEGL.so.1", RTLD_NOW|RTLD_NOLOAD)` +
`dlsym`), undtagen `eglGetProcAddress` selv (rekursionsfare), og tilbyder
stubs for `eglQueryDeviceStringEXT`/`eglQueryDisplayAttribEXT` (glxtest
kræver dem non-NULL og kalder queryDisplayAttrib direkte). Derudover er
glxtest-binæren patchet: dybde-tjekket `cmp r1, #24` → `#16` (fil-offset
0x2777, `0x18`→`0x10`; backup `/root/glxtest.orig`) — boksens X er 16-bit,
og uden patchen smed proben det ellers vellykkede EGL-resultat væk (Bug
1667621). Resultat: `glxtest` = PowerVR Rogue G6110 / GLES 3.1 /
TEST_TYPE=EGL.

**Ny blokering (24. aug):** WebRender-hardwarekontekst fejlede stadig →
"Fallback WR to SW-WR" (mønstre 0x300c/0x3000). Begge er nu LØST (25. aug):
0x300c via Android-bindAPI/chooseConfig-patches, 0x3000 via stub-libGL (se
fælde 24), og kompositorvinduets 1x1-frys via platformens live-størrelse.
WebGL 2.0 er nu STABILT virkende i normale kørsler (`WEBGL_RESULT OK ...
WebGL 2.0`, `present #2 (1280x948)`, korrekt titel). To fælder bag de
sidste "fejlslagne" kørsler: (a) `MOZ_GL_SPEW=1` lammer compositoren via
KHR_debug-callback (ingen present, siden loader ikke) — kør UDEN variablen;
(b) `pkill -9 -x firefox-esr` rammer KUN main-processen (børnene hedder
"GPU Process", "file:// Content", "Socket Process", "RDD Process" via prctl)
— når main dør, lukker børnene kanalen og skriver `Exiting due to channel
error.` + `_exit(0)` (og hybris display-dans kører). Det er altså et
oprydningsresultat, ikke en browser-race. Alle spor:
`devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-25.md`; DOK §5.15c;
`BROWSER-VEJE.md` eksperiment 2.

### Fælde 24: Firefox' GL-symboler snupper Mesa — kontekst-Init fejler stille

**Symptom:** fuld Firefox med hybris-EGL: `eglCreateContext` +
`eglMakeCurrent` (pbuffer) lykkes, men kontekst-`Init` fejler og Firefox
melder `Failed to create EGLContext!: 0x3000` (EGL-fejlen er 0, fordi fejlen
ikke er i EGL). Der kommer 0 `eglGetProcAddress`-kald efter MakeCurrent
(let at overse).

**Årsag:** Firefox' `SymbolLoader::GetProcAddress` (`GLLibraryLoader.cpp`)
slår navne op i `libGL.so.1` (dlsym) FØRST og kalder først
`eglGetProcAddress`, hvis dlsym fejler. Boksens `libGL.so.1` er Mesas
vendor-dispatch (allerede loadet, fordi hybris-wrapperens init kalder
`dlopen("libGL.so")`), så alle `gl*`-symboler peger på Mesa — og
`glGetError()`/`glGetString()` på en ikke-Mesa-kontekst fejler stille i
`InitImpl`.

**Kur (25. aug 2026):** tomme stub-`libGL.so` + `libGL.so.1` (ingen
gl*-eksporter, korrekt SONAME — `devuan/gpu/eglplatform_x11/build_stub_gl.sh`)
først i `LD_LIBRARY_PATH` (fx `/root/glstub:/opt/hybris`). Dlsym fejler på
hvert navn → fallback til `eglGetProcAddress` → wrapper → PowerVR GLES
(samme vej som glxtest, som var grøn). NB: stubben skal hedde BÅDE
`libGL.so` (wrapperens `dlopen`) og `libGL.so.1` (Firefox'
`PR_LoadLibrary`), og den må ikke eksportere gl*-navne — ellers vender
Mesa-symptomet tilbage.

**Relateret fælde (25. aug):** Firefox dlopen'er `libEGL.so` FØRST, derefter
`libEGL.so.1` — en trace-erstatning kun som `libEGL.so.1` bliver aldrig brugt.
Og `dlsym(libEGL-handle)` foretrækker bibliotekets egne eksporter frem for
LD_PRELOAD — interposer-vejen virker derfor ikke for EGL-symbolerne.

### Fælde 25: `MOZ_GL_SPEW=1` lammer WebRender-compositoren på hybris-stakken

**Symptom:** fuld Firefox med den grønne stak: shaders 61-64 kompilerer, men
der kommer ALDRIG en `x11ws: present`, siden loader ikke (titel forbliver
"Mozilla Firefox"), og alle processer sidder i vent (poll/condvar — målt med
gdb). Under strace virker det (timing ændres), hvilket fejlagtigt pegede på
en channel-error-race.

**Årsag:** `MOZ_GL_SPEW=1` får Firefox til at installere en KHR_debug-
callback, som på PowerVR-stakken stopper compositoren efter shader-
kompileringen. `MOZ_GL_SPEW` var en del af kørselskommandoen i session-notat
§6 fra før — den skal IKKE sættes.

**Kur (25. aug 2026):** kør uden `MOZ_GL_SPEW`. Derudover: `pkill -9 -x
firefox-esr` rammer kun main (børnene har prctl-titler), så `Exiting due to
channel error.` + display-dans ved kørslens slutning er oprydningsartefakt.
Og Firefox' kompositorvindue er depth 32 TrueColor — `XPutImage` skal bruge
vinduets visual/dybde + egen GC, ellers BadMatch og sort vindue (begge fixet
i `eglplatform_x11.cpp`).

**Kørsel som almindelig bruger (kristian):** hybris-stakken kræver desuden
enhedstilladelser — `/dev/pvrsrvkm`, `/dev/ion`, `/dev/pvr_sync` og
`/dev/video_state` til video-gruppen og `/dev/console` til tty-gruppen
(udev-regler; uden `/dev/pvr_sync` frigives overflade-buffere aldrig, og
skærmen forbliver tom trods `WEBGL_RESULT OK`). Og wrapperens `chvt()` er
patchet til at "lykkes" uden CAP_SYS_TTY (`/opt/hybris/libEGL.so*`: ioctl'erne
på 0x23c0/0x23e0 → `movs r0,#0; nop`; backup `/root/libEGL_hybris.orig`) —
file-caps dur ikke, fordi de sætter AT_SECURE og dermed slår vores
LD_PRELOAD/LD_LIBRARY_PATH-stak fra. Launcher: `/usr/local/bin/firefox-webgl`.

### Fælde 26: Ikke-root kan ikke oprette sockets (EACCES på `socket()`)

**Du ser:** Firefox som bruger kristian kan ikke nå nettet ("ingen internet"),
men root kan; `strace` viser `socket(AF_INET, SOCK_DGRAM) = -1 EACCES`.

**Årsag:** vores genbyggede 3.10-kerner har `CONFIG_ANDROID_PARANOID_NETWORK=y`
(marts-defconfig) — kun root (CAP_NET_RAW) eller gruppe 3003 (`inet`) må oprette
sockets.

**Fix:** `groupadd -g 3003 inet; usermod -aG inet kristian` + genstart sessionen
(07-scriptet gør det nu). Slå config'en FRA i næste kernel-byg.

### Fælde 27: Overskrivninger i system.img overlever ikke genstart (loop-mount)

**Du ser:** en fil du skrev ind i /system (via loop-mount, rw) er væk efter reboot
— men NYE filer overlever. (Loop/page-cache-aliasing på vendor-kernen.)

**Fix:** skriv overskrivninger med debugfs direkte i billedet:
```bash
umount /system; printf "rm <sti>\nwrite <lokal-fil> <sti>\n" > /tmp/x.cmd
debugfs -w -f /tmp/x.cmd /usr/local/share/libhybris/system.img
e2fsck -fy /usr/local/share/libhybris/system.img; mount -o loop,ro ... /system
```
Efter `mv` af billedet: detach/re-attach loop FØR skrivning. Billedet må aldrig
fyldes helt (var korrupt da det var 100 % fuldt).

### Fælde 28: Hybris-gralloc-headerne har forkerte GRALLOC_USAGE-værdier

**Du ser:** gralloc-alloc/lock fejler med EINVAL på usage-værdier som 0xCB.

**Årsag:** `/usr/local/include/android/hardware/gralloc.h` bruger forkerte værdier
(HW_FB=0x1000, SW_READ_OFTEN=0x3) i forhold til Android-standard (0x10 hhv. 0x80),
som 1.5-gralloc'en forventer. Det ramte x11ws' præsentation.

**Fix:** `eglplatform_x11.cpp` tvinger nu de korrekte konstanter (#undef/#define).

### Fælde 29: 3.10-compat mangler syscall 403 (clock_gettime64)

**Du ser:** konstant "syscall 403"-spam i dmesg fra 32-bit processer (fx Firefox'
GPU-proces), og bionic 6.0-kode kan fejle (fx gralloc-lock EINVAL).

**Årsag:** bionic 6.0-libc bruger clock_gettime64 (syscall 403); 3.10-kernens
compat-tabel har kun 384 poster.

**Fix:** `build_kernel.sh` udvider tabellen til 404 + 403 → `sys_clock_gettime`
(timespec64 matcher native på arm64). Verificeret: 0 "syscall 403"-spam.

### Fælde 30: 1.4-gralloc kan ikke bruges mod 1.5-stakken (SIGILL)

**Du ser:** 1.4-gralloc.rk3368.so kan ikke loade (mangler
`PVRSRVDeferredFreeDeviceMem` i 1.5-libsrv_um); selv efter symbol-patch → SIGILL.

**Løsning:** behold 1.5-gralloc'en (md5 `380658e4`). Præsentationsproblemet i
Firefox' GPU-proces er IKKE gralloc-versionen — se handoveren (gralloc-lock-sporet).

### Fælde 31: 1.5-shader-kompileren kan ikke heltals-varyings

### Fælde 32: `/dev/sw_sync` er 0600 root:root — gralloc-lock giver EINVAL for ikke-root
1.5-gralloc'ens `lock` laver en sw_sync-fence (CPU-læsning af
præsentationsbuffere). Firefox/GPU-processen kører som `kristian` →
`open("/dev/sw_sync")` = EACCES → lock returnerer -22 (EINVAL). Standalone som
root virker (derfor forvirrende). Fix: `chmod 666 /dev/sw_sync` — i myinit med
en retry-løkke, fordi enheden oprettes af kernen EFTER devtmpfs-mount.

### Fælde 33: `GRALLOC_USAGE_SW_READ_OFTEN=0x80` giver vaddr=NULL på 1.5
x11ws var patchet med Android-8-stil 0x80, men 1.5-gralloc'en (Android 5.1)
genkender kun 0x3 (SW-bit-maske 0x33) → lock rc=0 men vaddr=NULL → intet vist.
Brug 0x3.

### Fælde 34: GLESv2-proxyen brød WebGL1/ES1 — poki crashede ved load
`glesv2_proxy.c` (libGLESv2.so.2.0.0) fik ALLE poki-sider (SDK'ets
`getContext("webgl")`-probe) til at crashede Firefox ("WebGL actor Initialize
failed", channel error, minidump-generation fejler). Isoleret med lokal
webgl1-test. Fix: behold ORIGINAL libGLESv2 (md5 `ca71fb2c…`); EGL-proxyen har
selv shader-hooks via eglGetProcAddress.

### Fælde 35: alpha:false-WebGL-canvasser vises ikke med software-layers
Med `layers.acceleration.disabled=true` vises WebGL-canvasser med
`alpha:false` + `premultipliedAlpha:false` IKKE (tomt/transparent), mens
alpha:true vises. Spillet (PixiJS v8) anmoder alpha:false → shim tvinger
alpha:true. Symptom: spilområdet forsvinder/tomt trods at canvas'et renderer
(readPixels har indhold).

### Fælde 36: `--install-extension` og manuel extensions.json er upålidelige i ESR 140
Til test: indlæs udvidelsen via `about:debugging → Load Temporary Add-on`
(vælg manifest.json). Midlertidige udvidelser forsvinder ved genstart.

### Fælde 37: 3.10-kernen dræber processer ved ukendte compat-syscalls (fx clone3/435)
Firefox' `glean.upload`-tråd kaldte `clone3` (asm-generic nr. 435); 3.10-kernens
`do_ni_syscall` dræbte processen (SIGILL + minidump-forsøg) i stedet for at
returnere ENOSYS → hele Firefox crashede 1½–3 min efter start (målt 2× 6. sep).
Kun `dmesg`: `do_ni_syscall: … syscall 435` + registerdump af `glean.upload`.
Fix: kernel-compat-tabel udvidet til 450 poster; 404..449 →
`sys_ni_syscall` (ENOSYS) så glibc/Rust falder tilbage på `clone`.
`build_kernel.sh` har patchen; billede `out/clone3fix/ramfs-clone3fix.img`.

### Fælde 38: readPixels/toDataURL uden for frame er ubrugelig (preserveDrawingBuffer=false)
WebGL-canvas med `preserveDrawingBuffer:false` ryddes efter present → ekstern
readPixels giver variabelt ensartet sort/cyan og toDataURL altid sort. Mål i
stedet GL-side: efter-draw-readback i egl_proxy.c (postdraw-linjer i
`/tmp/cyan_draw_probe.log`) eller poki-frit replay-probe
(`scene_replay_probe.c`).

### Fælde 39: BiDi-session → poki `bot=1` → spillet fryser ved 0% ved reload
En WebDriver/BiDi-session får poki til at sætte `bot=1` i spil-iframe-URL'en;
reloader man siden under load, fryser Subway Surfers ved "Loading 0%" / blå
firkant uden netværksaktivitet. Load spillet NATURLIGT først; brug BiDi kun til
engangs-evaluering (scene/canvasdump) uden reload. Gentagne hurtige
Firefox-genstarter giver samme stall (server/rate) — kølepause 5+ min og evt.
ren profil (slet cache2/cookies/sessionstore).

### Fælde 40: `glGetBufferSubData` findes ikke via eglGetProcAddress på 1.5
Returnerer NULL (målt i replay-probe). Vertex-data må i stedet snappes ved
upload (`glBufferData`/`glBufferSubData`-hook). Proxy v3 med `glReadPixels`-hook
korrelerede desuden med load-stall → behold v2-funktionaliteten
(fbo/tex/draw-hooks; drawBuffers-logning md5 `715716d0` er nuværende).

**Du ser:** WebRender's `cs_blur`-vertex-shader fejler med kun "Compile failed."
(mens attributter + vec4[2]-retur virker).

**Årsag:** 1.5-kompileren afviser `flat varying ivec2/int` (heltals-varyings).

**Fix:** shader-omskrivning i `egl_proxy.c` (eglGetProcAddress-hook): vSupport
ivec2→vec2 + int()-casts. Generel regel: 1.5-kompileren er kræsen — verificér
konstruktioner isoleret med `compile_file_probe.c`/`vertex_tex_test.c`.

---

## 5. Fejlfinding: de fem første kommandoer

Når noget ikke virker, så kør disse fem, i denne rækkefølge, **før** du begynder at tænke:

```bash
df -h /                                    # 1. er disken fuld?      (fælde 1)
pgrep -a udevd                             # 2. er der én eller to?  (fælde 2)
grep -c "Adding extended input device" /var/log/Xorg.0.log   # 3. har X mus/tastatur?
pactl list sinks short                     # 4. alsa_output.dmixer eller auto_null? (fælde 3)
/usr/local/bin/fb_overscan.py --show       # 5. kører skærmkompensationen? (fælde 4)
```

Fem kommandoer, et halvt minut, og de dækker alt hvad der er gået galt indtil nu.

Hjælper det ikke: hent framebufferen hjem som et billede og se på den. Er panelet i
billedet men ikke på skærmen, ligger fejlen efter framebufferen — chip, kabel eller
fjernsyn. Er det heller ikke i billedet, er det skrivebordet der er noget i vejen med.

---

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

---

## 7. De tre store lærepunkter

**Skift mellem at læse kode og at måle.** Vi læste driverkode i timevis og byggede en
overbevisende teori om producentens grafikdriver. Fem farvede bjælker på skærmen og ét
spørgsmål om hvad der kunne ses, væltede den på to minutter. Når en teori bliver stor og
elegant, er det tid til at måle noget simpelt.

**Kontrollér altid at en rettelse virkede — og mål det rigtige sted.** Tre gange var
rettelsen på plads uden at have nogen effekt: en genvej der pegede ingen steder, en gruppe
uden det tilhørende program, et program i en startliste der ikke måtte åbne den fil det
skulle bruge. Ingen af dem gav en fejlbesked. Og en fjerde gang målte vi vores eget
testmiljø og meldte succes for tidligt.

**Sørg for at der er logs.** Halvdelen af aftenens tidsspild skyldtes at boksen ikke havde
nogen syslog-tjeneste, og at en fuld disk gjorde selv de tomme logfiler tomme. Et system
uden logning kan ikke fejlsøges — kun gættes på.

---

## 8. Hvis du vil vide mere

- `DOKUMENTATION.md` — den fulde tekniske historie, inklusive hvordan boot-kæden blev
  regnet ud, og alle fælder fra det tidligere arbejde.
- `DEBUG-SORT-SKAERM.md` — hele fejlsøgningen af den sorte skærm, med beviskæden og de
  kildehenvisninger der hører til.
- `DRIVER-PORTERING.md` — hvorfor vi ikke bare kan bruge en moderne Linux-kerne.
- `GRAFIK-FORKLARET.md` — grafikhistorien i hverdagssprog (god at læse selv eller højt).
- `TODO.md` — hvad der mangler.
- `git log` — hver commit forklarer hvad der blev rettet og hvorfor.
