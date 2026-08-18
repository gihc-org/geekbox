# Sort skærm efter flash — hele historien, forklaret fra grunden

Status: **løst 18. august 2026.** Denne fil forklarer hvad der var galt, hvordan vi fandt
ud af det, og hvilke fælder der kostede en hel dags fejlsøgning. Den er skrevet så den
kan læses uden at kende projektet i forvejen.

---

## 1. Det korte svar

Skærmen var aldrig i stykker. To små ting ramte os samtidig:

1. **Skrivebordsbaggrunden manglede**, så skrivebordet var helt sort.
2. **Fjernsynet klippede kanterne af billedet** — cirka 2 % hele vejen rundt. Den nederste
   bjælke med startmenu og ur er kun 26 pixels høj og ligger helt nede ved kanten, så den
   forsvandt ind i det bortklippede område.

Sort skrivebord plus en bortklippet bjælke = "sort skærm med kun en musemarkør".

Vi brugte en dag på at lede efter fejlen i grafik-driveren, fordi vores måleværktøj var
forkert og viste os noget der lignede en fejl i hardwaren. Det var det ikke.

---

## 2. Ordbogen: de ord du skal kende

**Framebuffer.** Et stykke hukommelse hvor billedet står, pixel for pixel. Skal skærmen
vise 1920x1080 punkter i 16 bit farve, er det 1920 × 1080 × 2 bytes ≈ 4 MB. Grafik-chippen
læser den hukommelse igen og igen, 60 gange i sekundet, og sender indholdet ud gennem
HDMI. Vil man vide hvad computeren *forsøger* at vise, kigger man i framebufferen. Den
heder `/dev/fb0` på boksen.

**X (eller Xorg).** Det program der styrer skærm, mus og tastatur på Linux. Alle vinduer
tegnes af X ned i framebufferen. Vores X bruger en simpel driver der heder `fbdev`, som
bare skriver direkte i framebufferen — ingen grafik-acceleration.

**lxpanel.** Bjælken i bunden af skrivebordet med startmenu, ur og programikoner. 26
pixels høj som standard. Det var den der "manglede".

**pcmanfm.** Programmet der tegner selve skrivebordet, altså baggrundsbilledet og ikonerne.

**Kernen og driveren.** Kernen er Linux' inderste del. Driveren er den del af kernen der
taler med grafik-chippen. Vores kerne er fra 2015 og leveret af producenten (en
"vendor-kerne"), ikke en officiel Linux-udgave. Den indeholder kode af blandet kvalitet —
undervejs finder vi både død kode og en bandeord i en fejlbesked.

**LCDC / VOP.** Grafik-chippens display-del i denne processor (en Rockchip RK3368). Den
læser framebufferen og laver HDMI-signalet. `win0` er dens vigtigste "lag" — det er det
lag der viser vores skrivebord.

**Overscan.** En gammel tradition fra billedrørs-tv: fjernsynet zoomer en lille smule ind
på billedet og klipper kanterne af, fordi gamle tv-billeder havde rod i yderkanterne.
Moderne fjernsyn gør det stadig, når de tror de får et *tv-signal* frem for et
*computer-signal*. Det var vores fejl nummer to.

**IOMMU og IOVA.** En IOMMU er en oversætter mellem de adresser en hardware-enhed bruger,
og de rigtige adresser i hukommelsen. En IOVA er en sådan "oversat" adresse: den ser ud
som en almindelig hukommelsesadresse, men peger et helt andet sted hen end den ser ud til.
Det blev vores største fælde.

---

## 3. Symptomet

Efter at have flashet boksen (skrevet et nyt system til dens indbyggede lager) viste
skærmen kun en musemarkør. Ingen skrivebordsbaggrund, ingen bjælke i bunden. Markøren
kunne bevæges normalt. Efter en genstart var billedet ofte fint.

Mønsteret "første boot er sort, næste boot er fin" pegede naturligt mod noget der
initialiseres forkert ved opstart. Det var et fornuftigt gæt. Det var også forkert.

---

## 4. Svaret, forklaret

### Fejl 1: den manglende baggrund

Skrivebordsprogrammet pcmanfm var sat op til at hente baggrundsbilledet fra stien
`/etc/alternatives/desktop-background`. Den sti er et *symlink* — en genvej der peger på
den rigtige billedfil. Men genvejen oprettes af en pakke, `desktop-base`, som vi ikke
installerer. Uden den peger stien ingen steder, og pcmanfm tegner så bare sort.

Det gjorde symptomet meget værre end det behøvede at være: **et sort skrivebord ser
fuldstændig ud som et ødelagt display.** Vi kunne ikke skelne "grafikken virker ikke" fra
"baggrunden er sort", og det sendte os på jagt efter en hardware-fejl.

Fix: lav symlinket til LXDE's eget tapet. Det ligger nu i byggescripterne `07` og `09`.

### Fejl 2: fjernsynet klipper kanterne

Fjernsynet (et 4K Samsung) viser ikke hele billedet. Det klipper cirka 25 pixel-linjer af
i toppen, 25 i bunden og cirka 48 pixels i hver side — omkring 2,3 % hele vejen rundt.

Panelet i bunden er 26 pixels højt. Klipper man 25 væk, er der 1 pixel tilbage. Derfor
"jeg kan se cirka 2 pixels af panelet". Startmenu-ikonet sidder i venstre hjørne på
x=0-40, og de yderste 48 pixels i siden er også væk — derfor var ikonet også usynligt.

Det er **fjernsynets** valg, ikke boksens fejl. Boksen sender hele tiden et korrekt
1920x1080-billede. Det tjekkede vi til sidst ved at hente framebufferen ned og se på den
som et billede: alt var på plads, helt ud i alle fire kanter.

Vigtig detalje: **beskæringen sker ikke hver gang.** Fire boots i træk samme aften:
klippede — fin — klippede — fin. Fjernsynet beslutter sig ud fra HDMI-forhandlingen ved
opstart, og den falder forskelligt ud. Det er hele forklaringen på "første boot er sort,
næste boot er fin", som vi ellers troede var en driver-fejl.

---

## 5. Hvordan vi fandt ud af det

Den afgørende metode var enkel: **tegn farvede bjælker i framebufferen i kendte højder,
og spørg hvad der kan ses på skærmen.** Er indholdet i framebufferen korrekt, men noget
ikke synligt, ligger fejlen efter framebufferen — altså i chippen, kablet eller
fjernsynet.

Vi tegnede hvide, røde, grønne og blå bjælker med kendt afstand til kanten:

| Hvad blev tegnet | Hvor i framebufferen | Kunne det ses? |
|---|---|---|
| Hvid ramme, 4 pixels | yderste kant, y=0-3 og y=1076-1079 | **nej** |
| Rød bjælke, 10 pixels | y=10-19 og y=1060-1069 | **nej** |
| Grøn bjælke, 10 pixels | y=30-39 og y=1040-1049 | ja |
| Startmenu-ikonet | x=0-40, altså i venstre kant | **nej** |

Læs tabellen sådan: alt inden for cirka 20 pixels fra kanten er væk, alt uden for 30
pixels er der. Beskæringen er altså 20-30 pixels, og den er **ens i top og bund** — og
den rammer også vandret.

Den symmetri er nøglen. En fejl i boksens timing ville forskyde billedet eller lade det
"vikle" rundt, ikke klippe lige meget af i begge ender. Symmetrisk beskæring på alle fire
kanter er præcis hvad overscan i et fjernsyn gør.

Til sidst hentede vi hele framebufferen ned som et billede og kiggede på den. Det
afgjorde alt: panelet stod der, med ur og ikoner, i fuld bredde. Boksen tegnede korrekt.

---

## 6. Fixet, og hvorfor det ser ud som det gør

Den *rigtige* løsning er at slå overscan fra i fjernsynets menu (Samsung: Billede →
Billedstørrelse → Skærmtilpasning, eller omdøb HDMI-indgangen til "PC"). Men den
indstilling er ikke altid til rådighed, og man vil helst ikke være afhængig af en
tv-menu. Så vi løser det i boksen: **vi gør billedet en smule mindre, så fjernsynets
beskæring kun spiser en sort kant.**

### Den blindgyde vi først prøvede

Driveren har allerede en indbygget funktion til netop det, og man styrer den fra filen
`/sys/class/display/HDMI/scale`. Vi skrev 95 i den. Den svarede pænt "xscale=95
yscale=95" — og der skete ingenting. Kigger man i kildekoden
(`drivers/video/rockchip/rk_fb.c`), forstår man hvorfor:

```c
int rk_fb_disp_scale(u8 scale_x, u8 scale_y, u8 lcdc_id)
{
	rk_fb_get_prmry_screen(&primary_screen);
	if (primary_screen.type == SCREEN_HDMI)
		return 0;                     /* altid sandt på en tv-boks */
	pr_err("fuck not be hear--%s\n", __func__);
	return 0;                             /* og ellers også */
	... 40 linjer død kode med den rigtige skalering ...
}
```

Funktionen giver op med det samme, når skærmen er tilsluttet via HDMI — altså altid, på
en tv-boks. Koden der faktisk kunne skalere, står nedenunder og bliver aldrig nået.
Producenten har efterladt en halvfærdig funktion, og en fejlbesked med et bandeord.

### Vejen der virker

I samme fil ligger `rk_fb_set_par()`, som programmerer grafik-lagets størrelse og
placering. Den læser de tal ud af to felter i framebufferens opsætning:

```c
xsize = (var->grayscale >>  8) & 0xfff     ysize = (var->grayscale >> 20) & 0xfff
xpos  = (var->nonstd    >>  8) & 0xfff     ypos  = (var->nonstd    >> 20) & 0xfff
data_format = var->nonstd & 0xff           /* denne del skal bevares, den er 4 hos os */
```

To felter der egentlig betyder noget helt andet (`grayscale` = gråtoner, `nonstd` =
ikke-standard format), er altså genbrugt til at pakke fire tal ind i. Det er en
producent-udvidelse, ikke almindelig Linux. Men den virker: sætter man felterne fra et
program, viser chippen framebufferen nedskaleret i et centreret vindue, og bruger sin egen
indbyggede skalerings-enhed til det.

Det gør `devuan/fb_overscan.py`:

```bash
fb_overscan.py                 # 95 %: billedet fylder 1824x1026, centreret
fb_overscan.py --percent 97    # mindre kompensation, mere billede
fb_overscan.py --reset         # tilbage til fuld skærm
fb_overscan.py --show          # vis tilstand, rør ingenting
```

Målt bagefter i driverens egne registre: vinduet blev 1824x1026 på position 48,27 med
skaleringsfaktorer 4310 og 4309 (4096 betyder 1:1, større tal betyder nedskalering).
X tegner stadig 1920x1080 og ved intet om det, så musen passer stadig med billedet.

### To krav der ikke er til at gætte

1. **Programmet skal køre EFTER X er startet.** Starter X bagefter, nulstiller den
   felterne. Derfor hænger vi det op i `/etc/xdg/lxsession/LXDE/autostart` — listen over
   programmer skrivebordet starter. Uden `@` foran, for `@` betyder "start igen hvis den
   dør", og vores program skal kun køre én gang.
2. **Brugeren skal være i gruppen `video`.** Filen `/dev/fb0` ejes af `root:video`, og er
   man ikke i gruppen, må man ikke skrive i framebufferen. Første gang glemte vi det, og
   så fejlede programmet **tavst** i autostart: intet i loggen, ingen effekt, alt så ud
   som om det aldrig var installeret. Det er nu ordnet i `07` (ved oprettelsen af
   brugeren) og i `09` (som en sikkerhedsnet-rettelse for ældre rootfs'er).

---

## 7. Fælderne — læs denne del før næste fejlsøgning

### 7.1 `/dev/mem` rammer ikke framebufferen (den dyreste fejl)

Man kan læse computerens fysiske hukommelse gennem filen `/dev/mem`. Filen
`/sys/class/graphics/fb0/phys_addr` fortæller hvor framebufferen ligger — den sagde
`0x10000000`. Så læste og skrev vi der. Det virkede tilsyneladende, og gav os et
overbevisende billede af at kun de øverste ~950 linjer nåede skærmen.

Det var forkert. Grafik-chippen bruger en **IOMMU** (`rockchip,iommu-enabled = <1>` i
hardware-beskrivelsen, og enheden `ff930300.vop_mmu` er aktiv). Derfor er `phys_addr`
ikke en fysisk adresse, men en **IOVA** — en oversat adresse. Driveren sætter tallet til
det som `ion_map_iommu()` returnerer (`rk_fb.c:3755`). Det ser ud som en adresse, men
peger et andet sted end det ser ud til.

Beviset er kort. Vi skrev det samme mønster gennem den rigtige vej og læste det gennem
`/dev/mem` på samme sted:

```
via mmap på /dev/fb0 : 3412341234123412
via /dev/mem         : 0000000004000000
```

To forskellige stykker hukommelse. **Alle målinger bygget på `/dev/mem` var altså
målinger på tilfældig hukommelse, ikke på billedet.** Hele teorien om at billedet blev
afkortet stammede derfra. Der var aldrig nogen afkortning.

Sådan gør man i stedet: `mmap` på `/dev/fb0`. Så går man gennem kernen, som selv finder
den rigtige hukommelse.

```python
import mmap, os
m = mmap.mmap(os.open("/dev/fb0", os.O_RDWR), 3840*1080)
m[y*3840:(y+1)*3840] = b"\xe0\x07"*1920      # en grøn linje på højde y
```

Lille detalje der kan narre: `m.flush()` giver fejlen EINVAL på en enhedsfil. Det er
harmløst — skrivningen er allerede sket. Vi troede først at hele skrivningen fejlede.

### 7.2 `uboot-logo-on = 0` gør at boksen ikke booter

Under fejlsøgningen fandt vi noget rigtigt i kildekoden: kernen programmerer aldrig selv
skærmens timing på denne boks, fordi den stoler på at opstartsprogrammet (U-Boot) allerede
har gjort det. Det styres af flaget `rockchip,uboot-logo-on` i hardware-beskrivelsen.

Vi slog flaget fra, byggede et nyt image og flashede. **Boksen bootede ikke** — lysdioden
blev lilla og aldrig blå. Årsagen er at flaget også styrer U-Boots *egen* opsætning af
skærmen, i en funktion (`board_fbt_preboot()`) hvis kode slet ikke findes i det kildetræ
vi har. Adfærden kunne altså ikke læses ud af kilden — den måtte måles, på den hårde måde.

Lære: rør ikke ved et flag der læses af to programmer, hvis du kun kan se kildekoden til
det ene. Rulle tilbage med `devuan/rollback.sh`, som sætter flaget til 1 og flasher igen.

Byggescriptet `09` har nu `DTB_PATCH=policy|logo|none`, hvor `logo` advarer om netop det.

### 7.3 `y_vir=960` betyder ikke 960 linjer

Driveren har en statusfil, `/sys/class/graphics/fb0/disp_info`. Den viste `y_vir: 960`, og
da skærmen er 1080 linjer høj, så det ud som en forklaring på et afkortet billede.

Det er ikke en højde. Det er linjelængden i hukommelsen, målt i enheder af 4 bytes:
1920 pixels × 2 bytes ÷ 4 = 960. Helt normalt. De felter der faktisk fortæller om
størrelsen, står i samme fil og heder `y_act` og `dsp_y` — og de sagde begge 1080, altså
korrekt, på både gode og dårlige boots.

### 7.4 `disp_info` er hardware, ikke gætteri

Vi antog længe at `disp_info` blot viste driverens interne opfattelse. Den læser faktisk
chippens registre direkte (`lcdc_readl` i `rk3368_lcdc.c:3611-3642`). At god og dårlig
boot ser ens ud dér er derfor et **rigtigt** resultat: grafik-laget var altid korrekt
opsat. Det burde have flyttet mistanken væk fra boksen langt tidligere.

### 7.5 `cat /dev/fb0` viser kun den øverste halvdel

Læser man framebufferen med `cat`, får man 2073600 bytes — 540 linjer, ikke 1080. Kernen
begrænser almindelig læsning til feltet `screen_size`. Man tror man har hele billedet, men
har halvdelen. Brug `mmap`.

### 7.6 En backup i konfigurationsmappen giver et ekstra panel

Vi lavede en sikkerhedskopi af panelets opsætning som `panel.bak` — i samme mappe som
originalen. lxpanel indlæser **hver fil** i mappen `/etc/xdg/lxpanel/LXDE/panels/`, så vi
fik to paneler: et med den nye opsætning og et med den gamle. To ure, to sæt ikoner, stor
forvirring. Læg altid backups uden for den mappe et program læser.

### 7.7 Et højere panel giver to rækker, ikke et højere panel

Som midlertidig lap satte vi panelets højde fra 26 til 64 pixels, så det kunne overleve
beskæringen. lxpanel svarede med at lægge ikoner og vinduesknapper i **to rækker**. Og
baggrundsbilledet blev *fliselagt* i stedet for strakt, så gradienten gentog sig og
lignede striber. Skal panelet være højt, skal `background=0` sættes (så temaet maler
baggrunden) og `usefontcolor=0` (ellers står hvid tekst på lys baggrund, og uret er
usynligt).

### 7.8 dmesg er ubrugelig efter at skrivebordet er startet

Kernens log (`dmesg`) druknes: kernen er fra 2013 og kender ikke et systemkald som alle
moderne programmer bruger (`clock_gettime64`), og hver gang det kaldes, skriver den et
komplet register-dump i loggen. Det er harmløst, men det skyller alle opstartsbeskeder ud
i løbet af sekunder. Derfor gemmer `myinit.sh` hele `dmesg` i `/root/bootlog.txt`
**før** skrivebordet starter.

### 7.9 Ting vi prøvede som ikke betød noget

Varm fontcache, panelkonfiguration i brugerens hjemmemappe, genstart af lxpanel og af X,
`fbset`-tricks, tænd/sluk via sysfs, af- og påmontering af driveren, HDMI-stikket ud og
i. Alt uden effekt, fordi problemet slet ikke var i boksen. Listen står her, så ingen
gentager den.

---

## 8. Det vi lærte om driveren undervejs (stadig sandt)

Dette forklarer ikke den sorte skærm, men det er rigtigt, og det forklarer en anden fejl:

**Kernen programmerer aldrig selv skærmens timing på denne boks.** Fordi flaget
`uboot-logo-on` er sat, stoler den på opstartsprogrammets opsætning:

- `rk3368_lcdc.c:2239` — kernen sætter kun pixelklokken, ikke hele timingen.
- `rk3368_lcdc.c:401-415` — den *læser* skærmens opløsning ud af de registre U-Boot
  efterlod, i stedet for at regne den ud selv.
- `rk_fb.c:3543-3561` — ved tilslutning af HDMI programmeres timingen kun, hvis
  opløsningen afviger fra den arvede.
- `rk_fb.c:2856` — flaget nulstilles kun af en funktion som Androids grafik-lag kalder.
  Vores X kalder den aldrig, så kernen bliver i "stol på opstartsprogrammet"-tilstand hele
  tiden, boksen er tændt.

**Følgen, som vi har set:** slukker skærmen efter 10 minutters inaktivitet
(strømsparefunktionen DPMS), kommer billedet ikke igen, for det kræver netop den
programmering der springes over. Derfor er skærmslukning slået fra i `fbdev.conf`
(`BlankTime 0` med flere).

Vil man tvinge kernen til at programmere timingen forfra, skifter man opløsning **til en
anden værdi** og tilbage:

```bash
cat /sys/class/display/HDMI/modes            # se de gyldige navne
echo 1280x720p-60  > /sys/class/display/HDMI/mode
echo 1920x1080p-60 > /sys/class/display/HDMI/mode
```

At skrive den opløsning der allerede er sat gør ingenting — driveren ser at intet er
ændret. Det var derfor et tidligere forsøg med samme kommando "ikke virkede": rigtigt greb,
forkert værdi. Grebet fjerner i øvrigt **ikke** fjernsynets overscan.

---

## 9. Værktøjskassen

```bash
# Se hvad boksen TEGNER (ikke hvad skærmen viser):
#   python3: m = mmap.mmap(os.open("/dev/fb0", os.O_RDWR), 3840*1080)
#            m[y*3840:(y+1)*3840] = b"\xe0\x07"*1920        # grøn testlinje
# Hent billedet hjem og se på det — det afgør de fleste spørgsmål på et minut:
#   på boksen:  konvertér framebufferen til en .ppm-fil (RGB565 -> P6)
#   hjemme:     scp filen hjem, og åbn den med PIL
# Driverens tilstand, læst direkte fra chippens registre:
cat /sys/class/graphics/fb0/disp_info        # HELE filen, ikke kun de første linjer
fbset -fb /dev/fb0 -i                        # timing og framebufferens størrelse
cat /sys/class/graphics/fb0/screen_info      # opløsning og billedfrekvens
cat /sys/class/display/HDMI/mode             # nuværende opløsning
cat /sys/class/display/HDMI/modes            # dem skærmen tilbyder
/usr/local/bin/fb_overscan.py --show         # er kompensationen slået til?
od -An -tx1 /proc/device-tree/fb/rockchip,uboot-logo-on   # hardware-flaget
# ADVARSEL: brug ALDRIG /dev/mem med fb0/phys_addr — det er en IOVA (se fælde 7.1)
```

---

## 10. Opskrift hvis skærmen ser tom ud igen

1. **Er baggrunden blå?** Er den sort, mangler symlinket
   `/etc/alternatives/desktop-background`.
2. **Kør `fb_overscan.py --show`.** Står der "fuld skærm", kørte kompensationen ikke.
   Kør den manuelt og se om panelet kommer frem. Gør det det, var det fjernsynets
   beskæring.
3. **Tegn farvebjælker i kendte højder** og se hvilke der kan ses. Det tager to minutter
   og udelukker halvdelen af alle hypoteser — inklusive dem vi selv brugte en dag på.
4. **Hent framebufferen hjem som billede.** Er panelet i billedet, men ikke på skærmen,
   ligger fejlen efter framebufferen: chip, kabel eller fjernsyn. Er det heller ikke i
   billedet, er det et program-problem i skrivebordet.

Og den vigtigste: **skift mellem at læse kildekode og at måle.** Vi læste driverkode i
timevis og byggede en overbevisende teori om producentens grafik-driver. Fem farvede
bjælker og ét spørgsmål om hvad der kunne ses, væltede den på to minutter.

---

## 11. Hvorfor virker det nu? (verificeret på boks 4, 19. august 2026)

En frisk boks blev flashet med det færdige image, og alt var rigtigt fra første boot. Her
er kæden af årsag og virkning, så det er tydeligt hvad der løser hvad — og hvorfor det
kræver **tre** ændringer, ikke én.

| # | Hvad gik galt | Hvorfor | Rettelsen | Hvor i repoet |
|---|---|---|---|---|
| 1 | Skrivebordet var helt sort | pcmanfm's baggrund peger på et symlink der laves af en pakke vi ikke installerer | symlink til LXDE's eget tapet | `07` + vagt i `09` |
| 2 | Bjælken i bunden var væk | fjernsynet klipper ~25 px af top og bund, og bjælken er kun 26 px høj | `fb_overscan.py` skrumper billedet til 95 %, så beskæringen kun spiser sort kant | `fb_overscan.py`, lagt i autostart af `07` + `09` |
| 3 | Rettelse 2 gjorde ingenting | programmet må skrive i `/dev/fb0`, som ejes af `root:video`, og brugeren var ikke i gruppen — så det fejlede **tavst** i autostart | brugeren føjes til `video`-gruppen | `07` (ved `useradd`) + vagt i `09` |

Rettelse 3 er den lærerige. Rettelse 1 og 2 var på plads i det image vi flashede først, og
det så alligevel ud som om ingenting var sket. Der var ingen fejlbesked nogen steder:
lxsession starter programmet, programmet kan ikke åbne framebufferen, og så dør det
stille. Vi opdagede det kun fordi vi tjekkede *resultatet* (`fb_overscan.py --show` sagde
"fuld skærm") i stedet for at antage at en installeret rettelse også virker.

Så på den nye boks ser målingerne sådan ud, og det er præcis det man skal se:

```
overscan-kompensation : vindue 1824x1026 på position 48,27      (kørte af sig selv)
video-gruppen         : 44(video)
tapet                 : /usr/share/lxde/wallpapers/lxde_blue.jpg
win0 i chippen        : dsp_x 1824, dsp_y 1026, x_st 48, y_st 27, faktor 4310/4309
ur                    : 19. aug 00:13 CEST                      (chrony har synkroniseret)
```

Læg mærke til at `win0`-linjen kommer fra grafik-chippens egne registre. Den siger at
chippen faktisk *gør* det programmet bad om — ikke bare at programmet mente det godt.

### Tre ting mere vi lærte på den nye boks

**`sudo` var slet ikke installeret.** Script `07` lægger brugeren i `sudo`-*gruppen*, men
selve pakken var aldrig kommet med, så boksen svarede "sudo: kommandoen ikke fundet".
Gruppen alene gør intet. Nu står `sudo` i pakkelisten (`extra_packages.sh`), og `09`
stopper bygningen hvis den mangler. Bemærk: adgangskoden er `geekbox` fra `07` og bør
skiftes med `passwd` for både `kristian` og `root`.

**Boksen har ingen MAC-adresse i sin hardware.** Kernen skriver ved hver boot: `Read the
Ethernet MAC address from IDB:00:00:00:00:00:00` — og laver så en tilfældig adresse.
Følgen er en ny DHCP-adresse ved hver boot, hvilket kostede tid i aftenens fejlsøgning,
fordi vi ledte efter boksen på dens gamle MAC. Derfor leder `devuan/find_box.sh` efter
dropbear-bannere i stedet, og derfor skal man ikke stole på routerens klientliste.

**Fjernsynets beskæring er ikke konstant.** Fire boots samme aften: klippede, fin,
klippede, fin. Det er derfor "første boot er sort, næste boot er fin" — ikke en fejl i
opstarten, men et fjernsyn der forhandler forskelligt fra gang til gang. Kompensationen
løser begge tilfælde, på bekostning af en sort kant også de gange fjernsynet ville have
vist hele billedet.

### Den generelle lære

Alle tre rettelser deler et mønster: **en indstilling var på plads, men den havde ingen
effekt**, og intet sted stod der en fejlbesked.

- Symlinket pegede et sted hen hvor der ikke var nogen fil.
- Brugeren var i `sudo`-gruppen, men programmet fandtes ikke.
- Programmet stod i autostart, men måtte ikke åbne den fil det skulle bruge.

Derfor står der nu **vagter** i `09`: bygningen stopper hvis `sudo`, tapetet eller
`video`-gruppen mangler. En vagt der stopper bygningen er meget billigere end en flash,
en boot, en tur til fjernsynet og en times fejlsøgning.

## 12. Det der stadig er åbent

- **Skal overscan-kompensationen køre altid?** Den er lagt i autostart med 95 %. Fordelen
  er at panelet aldrig forsvinder, uanset hvordan fjernsynet opfører sig. Prisen er en
  permanent sort kant (~27 pixels i top og bund, ~48 i siderne) og en let blødere skrift,
  fordi billedet nedskaleres. Alternativet er at tage linjen ud af autostart og kun køre
  programmet på de boots hvor panelet mangler — det er cirka halvdelen. **Dit valg.**
- **Den pænere løsning er fjernsynets menu:** Billede → Billedstørrelse →
  Skærmtilpasning, eller omdøb HDMI-indgangen til "PC". Så kan kompensationen slås helt
  fra og billedet bliver skarpt i kanterne.
- `rockchip,disp-policy = <0>` ville få kernen til selv at programmere timingen uden at
  røre opstartsprogrammet (og dermed formentlig løse DPMS-fejlen). Kan bages ind med
  `DTB_PATCH=policy`. **Ikke afprøvet på hardware.**
