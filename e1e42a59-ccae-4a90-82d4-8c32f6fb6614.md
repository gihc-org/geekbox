# Linux installation på Geekbox TV-boks

**ID:** `e1e42a59-ccae-4a90-82d4-8c32f6fb6614`  
**Oprettet:** 26. June 2026, 14:28  
**Sidst opdateret:** 27. June 2026, 18:06

---

### Du

Jeg har en geekbox tv boks med de specs som er vist på billedet. Den har Android installeret. Jeg har installeret Linux på en enhed magen til, men kan ikke huske hvordan. Jeg købte dem for flere år siden, og deres hjemmeside findes ikke mere, kan du hjælpe

---

### Claude

*[Brugte værktøj: web_search]*

Boksen er en **GeekBox (RK3368)** — den lille åbne TV-boks fra omkring 2015-2016, dual-boot Android/Ubuntu. Den officielle geekbox.tv-side er væk, men der findes stadig spor andre steder:

**Sådan flasher man den (genopfrisker hukommelsen):**

1. **Firmware/ROM** – GeekBox-teamet udgav tre images dengang: Light BizOS, Android+Ubuntu dual-boot, og rent Android. Disse lå oprindeligt på GitHub under `geekbox`-organisationen og blev spejlet flere steder, bl.a. på GeekBox Users Forum, som har en download-tråd med firmware, tools og schematics. Prøv `geekbox.boards.net` (forum-arkivet lever stadig).

2. **Flash-værktøjet** er `upgrade_tool` (Rockchip's RKDevTool på Linux). Det fås fra en RKtools-repo under Geekboxzone's GitHub-konto.

3. **Selve flash-processen:**
   - Forbind et USB-kabel til micro-USB OTG-porten
   - Mens boksen er strømmet, holder du Update-knappen nede samtidig med at du trykker kort på Reboot, og slipper Update efter et par sekunder — det sætter den i flash-mode
   - Kør `upgrade_tool` med firmware-imaget, og den reboot'er to gange undervejs

4. **Hvis du vil ind i bootloader uden fysiske knapper** (typisk på selve TV-boks-kabinettet i stedet for dev-boardet): der er ofte et "Update"- eller "Recovery"-pinhole et sted, du kan trykke ind med en tandstik mens du tænder boksen.

**Kildemateriale der stadig findes:**
- GitHub: søg efter `geekbox` org/repos (kernel, dtb-filer, RKTools) — flere brugere har forket/gemt dem
- `en.opensuse.org/HCL:GeekBox` har tekniske detaljer om partitionslayout og mtdparts
- Linux mainline-kernen har faktisk en device-tree-fil for GeekBox (`rk3368-geekbox.dts`), hvis du vil prøve en nyere mainline-kerne i stedet for den gamle 3.10.79

Vil du have mig til at grave specifikt efter de originale firmware-zip-filer (Cross_Lollipop_BizOS osv.) eller efter den GitHub-org, der hostede SDK'en?

---

### Du

Jeg har fundet nogle images/ROMS her https://wiki.radxa.com/Rock/prebuilt_images og her https://geekbox.boards.net/thread/6/geekbox-downloads-firmware-tools-schematics. Findes der noget nyere, eller vil det være muligt at opgradere fra de gamle versioner

---

### Claude

Lad mig kigge på begge sider for at se hvad der faktisk er tilgængeligt.

*[Brugte værktøj: web_fetch]*

Den kræver JavaScript, så jeg kan ikke hente indholdet direkte. Lad mig prøve Radxa-siden og søge for at finde ud af, hvad der konkret ligger i forum-tråden.

*[Brugte værktøj: web_fetch]*

Vigtigt at fange det her: **Radxa Rock er ikke det samme som GeekBox.** Det er to forskellige boards fra forskellige producenter (Radxa vs. GeekBuying/GeekBox-teamet), selvom de begge bruger lignende Rockchip-SoC'er fra samme periode (Radxa Rock kører RK3188, Rock2 kører RK3288 — ikke RK3368 som din boks). Lad mig undersøge det lidt mere, og se om Radxa-images faktisk kan bruges, eller om det er en blindgyde.

*[Brugte værktøj: web_search]*

Det bekræfter min frygt — det er vigtigt at jeg er ærlig om det her, fordi forkert firmware kan ødelægge boksen permanent.

## Den korte version

**Radxa Rock-billederne kan du ikke bruge.** Det er et helt andet board fra en anden producent (Radxa, ikke GeekBuying/GeekBox). Selv om begge bruger Rockchip-SoC'er, er de forskellige chips med forskellig pin-out, partition-layout og driveropsætning. Man skal aldrig flashe firmware lavet til et andet SoC, selv hvis det ligner — det kan brick'e enheden, og Rock/Rock2/Rock Pro kører RK3188/RK3288, ikke din RK3368.

**Forum-tråden (geekbox.boards.net) er sandsynligvis dit bedste fund** — det er den officielle community-side specifikt for GeekBox-hardwaren. Jeg kunne ikke hente dens indhold direkte (siden kræver JavaScript at indlæse), så jeg kan ikke se *hvilke* filer der faktisk ligger der uden at du åbner den selv i en browser. Kan du kigge i tråden og fortælle mig, hvilke filnavne/versioner du ser? Så kan jeg hjælpe med at vurdere dem.

## Om "nyere" findes

Realistisk set: **nej, der kommer ikke noget nyere end det, der allerede blev udgivet 2015-2017.** GeekBox som projekt er dødt — firmaet eksisterer ikke længere, og der har ikke været officiel udvikling siden. De eksisterende muligheder er:

- **Originale GeekBox-images**: Light BizOS, Android+Ubuntu dual-boot, ren Android — alle fra 2015/2016
- **Mainline Linux-kernen** har faktisk stadig en device-tree-fil for GeekBox (`rk3368-geekbox.dts`), så det er teknisk muligt at bygge en moderne mainline-kerne selv — men det er et ret stort DIY-projekt, ikke en færdig ROM
- **Generiske RK3368-firmwares** fra andre bokse (fx fra freaktab.com) — disse *kan* nogle gange bruges på tværs af forskellige RK3368-enheder, men kun hvis WiFi-chip og hardware-layout matcher, og der er reel risiko for brick

## Kan man opgradere fra de gamle versioner?

Ja, det giver mening at gå efter en nyere Android (5.1 → evt. 6/7 fra en anden RK3368-enheds firmware), men det er ikke en walk in the park: det kræver typisk at man matcher WiFi-chip og CPU med donor-enheden, og selv fjernbetjeningen skal man hente fra sin egen originale firmware og indsætte i den nye.

Hvis dit primære mål er Linux (ikke en nyere Android), er det faktisk en bedre vej end at jagte Android-opgraderinger — for Linux-delen kan du potentielt bruge en nyere mainline-kerne i stedet for den gamle GeekBox-specifikke 3.10.79.

**Spørgsmål til dig:** Er målet at genskabe præcis den samme Ubuntu/Android dual-boot som din anden boks kører, eller er du åben for at prøve en nyere/anden Linux-distribution på den her?

---

### Du

Mit mål er at skrotte Android, og få en så ny Linux som mulig installeret på enheden

---

### Claude

**Det er en realistisk plan, men med et vigtigt forbehold:** "så ny som mulig" på en RK3368 fra 2015 betyder noget andet end på moderne hardware. Lad mig give dig det reelle billede, så du kan vælge med åbne øjne.

*[Brugte værktøj: web_search]*

Som forventet — Armbian's nuværende fokus er RK3588/RK3568/RK322X/RK3318 osv. Ingen omtale af RK3368 i de aktuelle releases. Lad mig tjekke specifikt om Armbian eller andre distroer overhovedet stadig understøtter RK3368 i dag.

*[Brugte værktøj: web_search]*

Det bekræfter billedet klart. Lad mig give dig hele konklusionen.

## Den ærlige status: RK3368 er reelt forladt af det moderne ARM-Linux-økosystem

**Armbian understøtter ikke RK3368.** Kun RK3399 og RK3288 er officielt supporteret blandt de ældre Rockchip-chips — RK3368 har aldrig fået officiel support, selv om folk har spurgt siden 2018. Sjovt nok dukker netop din GeekBox op som eksempel i en gammel Armbian-forumtråd fra 2016, hvor en udvikler overvejede arm64-support, men det blev aldrig til noget officielt produkt.

Det betyder konkret:
- Ingen Armbian-image til dig
- Ingen ophub/amlogic-s9xxx-armbian support (den er for Amlogic, ikke Rockchip alligevel, og en bruger der spurgte om RK3368-support i 2023 fik ingen løsning)
- Mainline-kernen *har* device-tree filen for GeekBox, men det er ikke det samme som "en distro du downloader og flasher"

## Dine tre realistiske veje, fra nemmest til hårdest

**1. Brug GeekBox's egen gamle Ubuntu/Linux-image (Lubuntu på kernel 3.10.79)**
Det er den image, du allerede har fundet i forum-tråden. Det er ikke "ny" Linux — det er en ti år gammel kernel — men det er det letteste, mest dokumenterede, mindst risikable valg. Du kan opdatere userspace ovenpå (apt update/upgrade til en nyere Ubuntu/Debian-pakkebase), selv om kernen forbliver gammel.

**2. Byg din egen mainline-kernel + en generisk arm64 rootfs (Debian/Ubuntu/Arch)**
Da mainline-kernen rent faktisk har `rk3368-geekbox.dtb`, kan du teoretisk:
- Cross-compile en moderne mainline-kernel (6.x) med den dtb
- Bygge/downloade en generisk Debian/Ubuntu arm64 rootfs (debootstrap)
- Sætte de to sammen manuelt med U-Boot

Dette er et reelt DIY-projekt — ikke triviel, men givet din baggrund (Rust, kernel-arbejde, kompileringsmiljøer) er det inden for det du kan klare. Risikoen er at perifere ting (WiFi-chip AP6354, GPU PowerVR/PVR, video-decode) sandsynligvis **ikke** virker med mainline — kun CPU, RAM, basal storage, Ethernet, USB er rimeligt sikre at få til at virke.

**3. Brug det generelle "RK3368 Linux"-community-spor (Linux-on-RKxxxx-bokse, fx libreelec/kszaq-stilen, eller postmarketOS-tilgang)**
Nogle hardcore community-distroer til Rockchip-TV-bokse (LibreELEC, eller folk der bygger custom rootfs til specifikke bokse) understøtter stadig RK3368 — typisk lavet til specifikke kommercielle TV-bokse (M8S Pro, Tronsmart osv.), men siden de deler SoC, kan kernen ofte genbruges selv om device-tree skal justeres til GeekBox.

## Min anbefaling

Givet at du allerede har gjort dette én gang før (på den anden boks), og givet dit mål er "skrot Android, så ny Linux som muligt" — vil jeg foreslå at vi:

1. Først tjekker forum-tråden grundigt for at se om der findes en **nyere brugerbygget** Lubuntu/Debian-image end den originale 2015-udgave (community-medlemmer arbejdede videre på det i flere år)
2. Hvis ikke, går vi efter spor 2 — bygge en moderne mainline-kernel + frisk rootfs, hvor du accepterer at WiFi/GPU formentlig ikke virker, men du får en ægte ny userspace og kernel

Vil du have mig til at gå i gang med at undersøge, hvad der præcis ligger i forum-tråden (hvis du kan poste filnavnene/listen du ser), eller vil du hellere starte direkte på mainline-kernel-sporet?

---

### Du

Hvor var linket til Lubuntu

---

### Claude

Godt spørgsmål — lad mig være præcis, for jeg har faktisk ikke et direkte downloadlink til en Lubuntu-image, jeg har kun *omtaler* af, at den findes.

Det jeg konkret har fundet er:

1. **CNX Software-artiklen** (fra 2015) nævner et firmware-navn: `Cross_Lollipop_BizOS_V151208.7z` — men det er BizOS (Rockchip's Android-baserede desktop-OS), ikke Lubuntu.

2. **AndroidTVBOX.eu-artiklen** nævner navnet **"Android & Lubuntu dual boot image – V151129"** som en af de tre originale ROM's GeekBox-teamet udgav i november 2015 — men jeg har ikke et faktisk downloadlink til den, kun navnet og datoen fra artiklen.

3. **Forum-tråden** (`geekbox.boards.net/thread/6/...`) er det sted, hvor selve filerne sandsynligvis ligger eller linkes til — men den kunne jeg ikke hente, fordi siden kræver JavaScript for at vise indhold. Det er derfor jeg bad dig kigge i den selv.

Lad mig prøve at finde det originale download-link til "Android & Lubuntu dual boot image" specifikt.

*[Brugte værktøj: web_search]*

Nu har jeg det. Her er de konkrete links:

## Det originale link (sandsynligvis dødt)

**Mediafire-linket fra GeekBuying's egen blog** (december 2015):
```
http://www.mediafire.com/download/crmbyl70qqn3crw/Cross_Lollipop_Lubuntu_dualOS_V151129.7z
```
Det er fra GeekBuying's egen "geekgiz" blog, hvor de listede Android og Ubuntu Stock ROM-linket direkte. Mediafire-links fra 2015 er ofte døde i dag, men det er værd at teste — Mediafire sletter ikke altid automatisk gamle filer.

## En nyere version findes faktisk

Det interessante er: der kom en **opdateret version** i januar 2016, ikke kun den originale fra november 2015. Et XDA-indlæg nævner "Geekbox_Lollipop_Lubuntu_dualOS_V160129" med en changelog der bl.a. forstørrer Ubuntu-partitionen til 4GB, tilføjer "reboot ramfs"-support, og opgraderer Ubuntu til 14.04.3. Den lå oprindeligt på Geekbox-forummet (`forum.geekbox.tv`), som nu er dødt, men måske dukker den op i `geekbox.boards.net`-tråden, du fandt.

## Andre spor værd at undersøge

- En guide til at flashe/installere Lubuntu findes muligvis stadig arkiveret fra geekbox.tv: "geekbox-flashing-or-installing-lubuntu" — værd at tjekke på Wayback Machine, da geekbox.tv er dødt
- Der findes en vejledning til at installere ArchLinux ARM på GeekBox, hvor man lægger den generiske ArchLinux-filsystem ovenpå den eksisterende Lubuntu update-image — men forfatteren rapporterer at lokal konsol, wifi og boot ikke fungerede helt, så det er ikke uden forbehold
- Der nævnes også en "Ubuntu MATE 16.04 (aarch64)" build af en bruger ved navn gmathews, hvilket ville være et reelt skridt op fra den 32-bit Lubuntu 14.04

**Vil du have mig til at:**
1. Teste om Mediafire-linket stadig virker
2. Søge Wayback Machine for de døde geekbox.tv/forum.geekbox.tv-sider for at finde V160129-imagen og ArchLinux/Ubuntu MATE-guides

---

### Du

Når jeg køre "./upgrade_tool uf /home/kristian/Downloads/Geekbox_Lubuntu_V160309/update.img" får jeg denne fejl:
Segmentation fault         (core dumped) ./upgrade_tool uf /home/kristian/Downloads/Geekbox_Lubuntu_V160309/update.img

---

### Claude

Det er en klassisk kompatibilitetsfejl med den gamle `upgrade_tool`-binær — den her opstår typisk fordi det er et gammelt 32-bit x86-binary fra 2015/2016, og den kører ikke rent på moderne 64-bit Linux-distroer uden de rigtige compat-biblioteker. Lad mig undersøge specifikt.

*[Brugte værktøj: bash_tool]*

*[Brugte værktøj: web_search]*

Generelle resultater bekræfter mistanken, men er ikke specifikke nok til denne `upgrade_tool`-binær. Lad mig gå mere målrettet efter Rockchip-specifikke rapporter om netop denne fejl.

*[Brugte værktøj: web_search]*

Godt — det giver mig en konkret, moderne, vedligeholdt erstatning. Lad mig fortælle dig, hvad jeg ser, og hvad jeg vil teste/anbefale.

## Diagnosen

Den `upgrade_tool`-binær du har, er fra 2015/2016 — den gamle, lukket-kildekode Rockchip-binær. Det er Rockchips proprietære lukket-kildekode-værktøj til at flashe images til SPI, eMMC, SD-kort osv., og i modsætning til det open source-alternativ rkdeveloptool giver det ikke adgang til kildekoden — kun den binære fil. Når sådan en gammel statisk-linket eller dynamisk-linket 32-bit binær køres på en moderne Ubuntu 24.04 64-bit, segfaulter den typisk fordi:

1. Den er **32-bit (i386)** og din maskine mangler de nødvendige i386-kompatibilitetsbiblioteker (`libc6:i386`, `libusb-0.1-4:i386` osv.)
2. Eller den linker mod en **gammel libusb-version** (libusb-0.1 fra 2015), som enten ikke findes eller er inkompatibel med moderne libusb-1.0/udev-stakken

Det er præcis den slags fejl, "Segmentation fault (core dumped)" uden andre fejlmeddelelser typisk indikerer ved ABI-mismatch.

## Tjek hurtigt hvad binæren faktisk er

Kør disse to kommandoer og send mig output:

```bash
file ./upgrade_tool
ldd ./upgrade_tool
```

`file` afgør om det er 32-bit (i386) eller 64-bit (x86_64) ELF. `ldd` viser hvilke biblioteker den mangler — typisk ser du noget i stil med `not found` ved et af bibliotekerne, hvilket er den reelle synder.

## Den sandsynlige fix, hvis det er 32-bit

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install libc6:i386 libusb-0.1-4:i386 libstdc++6:i386
```

## Det moderne alternativ (min reelle anbefaling)

I stedet for at jagte den gamle binær til at virke, er der et **aktivt vedligeholdt, open source, moderne alternativ** på GitHub: `vicharak-in/Linux_Upgrade_Tool`. Det understøtter samme kommandoer (UpgradeFirmware: UF, UpgradeLoader: UL, osv.) og er stadig under udvikling i dag, i modsætning til den gamle GeekBox-æra-binær der ikke er rørt siden 2016. Den er specifikt bygget til at fungere på moderne Debian/Ubuntu uden de gamle i386-kompatibilitetsproblemer.

Vil du have mig til at finde den nøjagtige installationsvejledning til `vicharak-in/Linux_Upgrade_Tool`, eller vil du først prøve `file`/`ldd`-diagnosen på din nuværende binær for at se om det bare er en hurtig biblioteksfix?

---

### Du

Ja tak jeg vil gerne have guiden til vicharak-in/Linux_Upgrade_Tool

---

### Claude

*[Brugte værktøj: web_fetch]*

Nu har jeg det fulde README. Her er den komplette guide til at komme i gang.

## Trin 1 — installer dependencies

Linux Upgrade Tool kræver libudev-dev og libusb-1.0-0-dev på Debian/Ubuntu.

```bash
sudo apt-get install libudev-dev libusb-1.0-0-dev
```

## Trin 2 — hent værktøjet

```bash
git clone https://github.com/vicharak-in/Linux_Upgrade_Tool
cd Linux_Upgrade_Tool
chmod +x upgrade_tool
```

Bemærk: det er stadig en **prækompileret binær** (samme princip som den gamle), men den er distribueret som en binær fil i et repo der stadig er aktivt og bygget til moderne systemer, til forskel fra rkdeveloptool som er fuld open source. Den her skulle dog være linket mod moderne libusb-1.0, hvilket er præcis det din gamle binær formentlig manglede.

## Trin 3 — tjek at boksen detekteres

Sæt GeekBox i Maskrom/Loader-mode (samme metode som tidligere: hold Update-knappen mens du strømmer/reset'er boksen), forbind USB OTG, og kør:

```bash
sudo ./upgrade_tool ld
```

Forventet output ser ud som "List of rockusb connected(1)" med en DevNo, Vid, Pid og Mode=Maskrom linje. Hvis den her kommando segfaulter ligesom den gamle, er problemet ikke binæren — det er noget andet (USB-permissions, kabel, eller selve enheden). Men hvis den her virker, har vi bekræftet diagnosen.

## Trin 4 — selve flashningen

To metoder, begge dokumenteret:

**A) Direkte update.img-metode (det du allerede prøvede):**
```bash
sudo ./upgrade_tool uf /home/kristian/Downloads/Geekbox_Lubuntu_V160309/update.img
```
Forventet output viser trin som "Download Boot Start/Success", "Wait For Maskrom", "Check Chip", "Get FlashInfo", "Prepare IDB", "Download IDB", og til sidst "Download Image... (X%)".

**B) Hvis A) stadig segfaulter, partition-for-partition metoden:**
```bash
sudo ./upgrade_tool db <loader_path>     # flash loader/bootloader først
sudo ./upgrade_tool ef                   # erase flash om nødvendigt
sudo ./upgrade_tool wl 0 <raw_image>.img # skriv raw GPT image
sudo ./upgrade_tool rd                   # reboot device
```
Denne metode kræver dog en separat loader-binær (`.bin`-fil specifik til RK3368), som ikke nødvendigvis findes i din GeekBox-pakke — det var typisk indbygget i `update.img` i den gamle workflow.

## Vigtigt forbehold

Værktøjet her er testet og dokumenteret mod **RK3399 og RK3568** (Vicharaks egne boards Vaaman/Vaaman). Det er den samme protokol-familie som RK3368 bruger, men der er ingen garanti for at firmware-headeren i din 2016-vintage GeekBox `update.img` er 100% kompatibel med denne nyere værktøjsversion — protokollen har ændret sig lidt over årene (man kan se det i "Support Type:330C" i eksemplet, hvor 330C er chip-ID for en anden SoC).

**Min anbefaling:** kør `ld` først som en hurtig, ufarlig test. Virker den uden segfault, er vi på rette spor og kan gå videre til `uf`. Send mig output fra `file ./upgrade_tool` på den nye binær også, så jeg kan bekræfte at den rent faktisk er bygget anderledes end din gamle.

---

### Du

sudo ./upgrade_tool uf /home/kristian/Downloads/Geekbox_Lubuntu_V160309/update.img Loading firmware... Support Type:330A       FW Ver:5.1.00   FW Time:2016-03-09 19:10:26 Loader ver:2.40 Loader Time:2015-11-12 16:57:57 Start to upgrade firmware... Test Device Start Test Device Success Check Chip Start Check Chip Fail Note:Communication issues,Certainly please check usb,Completely please check device

---

### Claude

Fremragende — det her er reelt fremskridt! Den nye binær virker (ingen segfault), og vi fik værdifuld information ud af det.

## Hvad output betyder

- **"Support Type:330A FW Ver:5.1.00 FW Time:2016-03-09"** — det her bekræfter at firmware-imaget er ægte og parsbart: V160309-imagen er fra 9. marts 2016, og 330A er chip-ID for RK3368-familien (RK330A er det interne Rockchip-navn for RK3368-loaderen).
- **"Test Device Start → Test Device Success"** — værktøjet kan se og kommunikere med boksen i Maskrom-mode. USB-forbindelsen virker grundlæggende.
- **"Check Chip Start → Check Chip Fail"** — her går det i stykker. Værktøjet spørger chippen "er du faktisk en RK3368/330A?" og får et svar, det ikke kan validere, eller intet meningsfuldt svar.

## Hvad det sandsynligvis betyder

Dette er ikke længere et software/ABI-problem (det har vi løst) — det er nu et **kommunikations-/hardware-niveau-problem**. De typiske årsager til "Check Chip Fail" efter en succesfuld "Test Device":

1. **USB-kablet er ikke godt nok** — mange micro-USB-kabler er kun til opladning (power-only, ingen data-pins). Det her er den klassiske fælde med Rockchip OTG-flashing.
2. **Forkert USB-port på computeren** — nogle USB3-porte/hubs er upålidelige til lavt-niveau loader-kommunikation; en direkte USB2-port på bagsiden af maskinen er ofte mere stabil.
3. **Boksen gled ud af Maskrom-mode** midt i processen, eller var ikke helt i Maskrom-mode, men i et andet (almindeligt loader-)mode der svarer anderledes.
4. **Strømforsyning** — hvis boksen ikke får nok strøm under flashing (fx hvis du strømmer den via en svag USB-port i stedet for den originale strømadapter), kan chip-kommunikationen blive ustabil.

## Hvad jeg vil bede dig prøve

**Spørgsmål til dig, så jeg ved hvilken vej vi går:**

*[Brugte værktøj: ask_user_input_v0]*

---
