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
punkter i 16-bit farve = cirka 4 MB. Grafikchippen læser den 60 gange i sekundet og sender
indholdet ud gennem HDMI. Vil du vide hvad boksen *forsøger* at vise, kigger du der. Den
heder `/dev/fb0`.

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

### Fælde 14: Småting der koster timer

- **`uboot-logo-on = 0` i DTB'en gør at boksen ikke booter.** Lysdioden bliver lilla og
  aldrig blå. Flaget styrer også bootloaderens egen skærmopsætning, i kode vi ikke har.
  Rollback: `devuan/rollback.sh`.
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
- `TODO.md` — hvad der mangler.
- `git log` — hver commit forklarer hvad der blev rettet og hvorfor.
