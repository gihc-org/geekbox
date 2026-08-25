# Grafikken på GeekBox — forklaret i hverdagssprog

Dette dokument fortæller historien om boksens grafik: hvorfor WebGL ikke virkede,
hvad vi gik igennem for at finde ud af det, og hvad vi kan i dag. Forklaringerne
bruger hverdagssprog og billeder i stedet for fagudtryk — de tekniske detaljer og
beviserne står i de dokumenter, der henvises til undervejs. Det er skrevet til at
læse selv, læse højt eller give videre.

---

## 1. Hvad er en GeekBox?

En GeekBox er en lille TV-boks fra 2015 — på størrelse med en hånd. Indeni sidder en
Rockchip RK3368-chip med otte små regnere (kerner), 2 GB hukommelse og to
grafik-maskiner, som vi skal lære at kende:

- **VOP'en** — "skærm-maskinen". Den læser et billede i hukommelsen og sender det ud
  gennem HDMI-stikket til fjernsynet, 60 gange i sekundet. Den er dum som en dør: den
  viser bare præcis de pixels, der står i dens hukommelse (framebufferen).
- **GPU'en** — "tegnefabrikken". En PowerVR G6110, en særlig maskine bygget til at
  tegne 3D-grafik lynhurtigt. Den tegner ikke af sig selv: den venter på ordrer.

De to maskiner kender ikke hinanden. Der skal oversættere imellem dem — og det er
hele historien.

## 2. Hvorfor virkede WebGL ikke?

WebGL er et **sprog**, browseren taler til tegnefabrikken: "tegn denne trekant i 3D,
hurtigt!" Firefox kan sagtens tale sproget (vores er ny og kan WebGL 2). Problemet var,
at der ikke var nogen til at *høre* det.

For at WebGL skal virke, skal der være tre ting:

1. **Døren ind til fabrikken** — en driver i kernen, der tager imod ordrer
2. **Fabrikkens maskiner** — de programmer, der oversætter "tegn en trekant" til
   fabrikkens eget hemmelige sprog
3. **En transportvej** — en måde at få det færdige billede fra fabrikken ud på TV'et
   (det moderne navn er KMS/DRI — det findes ikke i vores gamle kerne fra 2013)

Vi troede længe, at alt manglede. Men da vi målte på boksen, fandt vi ud af noget
sjovere: **døren stod faktisk åben** (kernel-driveren `pvrsrvkm` var loadet!), og
**fabrikkens maskiner lå gemt i producentens efterladte filer** (en hel lille Android
i en fil: `system.img`). Der manglede altså "kun" oversætteren imellem — men den del
tog en hel aften. (Detaljer: DOKUMENTATION.md §5.13 og §5.15.)

Og hvorfor kunne skrivebordet køre, når WebGL ikke kunne? Fordi skrivebordet ikke
bruger fabrikken — det maler billedet **i hånden** med CPU'en direkte i VOP'ens
hukommelse (den gamle fbdev-metode). Det svarer til at male et maleri med pensel
(CPU) i stedet for at lade fabrikken gøre det. Penslen virker altid — den er bare
langsom, og den kan ikke tegne 3D.

## 3. Rejsen — hvad vi gik igennem

En aften, otte opdagelser:

1. **Browseren var uskyldig.** Firefox 140 støtter WebGL fint. Beskeden "browseren
   understøtter ikke WebGL" betyder faktisk "jeg kan ikke få fat i et grafikkort".
   (DOK §5.13, HAANDBOG fælde 16)
2. **Fabrikken var der.** Kernel-driveren var loadet og dens hjælpetråde kørte — men
   den stod i tomgang, fordi ingen gav den ordrer. Som en bil med motor, men uden
   gearkasse og hjul. (DOK §5.15)
3. **Maskinerne lå gemt.** Vi hentede dualOS-imaget (en ældre Android-firmware) og
   fandt de lukkede PowerVR-programmer i den. Endnu bedre: producentens egen
   Lubuntu-firmware indeholdt en hel **hybris-bro** (et sæt oversættere, de selv
   havde bygget til Kodi) — klar til brug.
4. **Startknappen manglede.** Fabrikken skal startes af et helt bestemt program
   (`pvrsrvctl`). Uden det svarede kernen bare "init ikke færdig" for evigt — uden
   fejlbesked. Vi fandt det ved at læse kernel-kildekoden. (DOK §5.15)
5. **Fabrikkens lager var for lille.** Da vi endelig fik gang i den, kunne den kun
   holde **ét** helt skærmbillede i sit hurtige lager (CMA). Andet billede →
   "Operation not permitted" — uden nogen forklaring. Løsningen var at give kernen
   besked om at reservere mere plads (`cma=128M`) — én linje i boot-opsætningen.
6. **En dørklokke virkede ikke.** De gamle programmer ville tilkalde hjælpeprogrammer
   med `system()` — men den moderne glibc fejlede tavst i vores blanding af gammelt og
   nyt. Vi byggede en lille lappegrej (en "shim"), der tager imod opkaldene i stedet.
7. **Vi skrev vores eget program.** Ingen af producentens egne testprogrammer virkede
   på vores opsætning, så vi skrev `test_triangle` selv — og **det virkede**:
   `GL_RENDERER=PowerVR Rogue G6110`, 500 billeder i træk. Fabrikken tegnede!
8. **Skærmen overlevede ikke festen.** Da vi bagefter startede skrivebordet igen,
   var HDMI'en død: kernen troede, den sendte billede, men TV'et fik intet signal.
   Kun en strøm-cyklus kunne vække den. (DOK §5.15, boks 2's HDMI-erfaring i DOK §10)

Undervejs faldt der to sidehistorier af, som også er dokumenteret: boksen crashede
engang under en installation, og det viste sig at være **strømforsyningen**, der løj
om sine 2 ampere — bevist med et A/B-forsøg og en stress-test (DOK §5.14, HAANDBOG
fælde 17, `devuan/stress_test.sh`).

### Hvad laver Android-laget egentlig?

GPU'ens programmer (blobs'ene) er maskiner, bygget til at stå i en Android-fabrikshal —
og de er forseglede, så vi kan ikke bygge dem om. Når de kører, forventer de hallens
omgivelser:

- **bionic** (`/system/lib`) — hallens eget sprog: Androids egen lille udgave af
  C-biblioteket, anderledes end Linux'. Maskinerne taler kun dét.
- **logd** — hallens opslagstavle, hvor maskinerne skriver deres beskeder (vi fangede
  selv CMA-fejlen på tavlen under fejlsøgningen).
- **servicemanager** — hallens reception: et register, maskinerne spørger om tjenester ved.
- **build.prop** — hallens opslag med indstillinger.
- **ION + gralloc** — hallens lagerhal: Androids måde at uddele hukommelsesblokke på.
- **pvrsrvctl** — værkføreren, der tænder fabrikkens hovedafbryder.

`system.img` er et stykke af den fabrikshal, stillet ind i vores Linux-bygning — og
**libhybris er gangen mellem de to bygninger**: den lader et Linux-program læsse de
Android-byggede maskiner ind i sin egen proces, med en indbygget Android-tolk (linker),
der finder maskinernes bionic-afhængigheder. Vi kan ikke springe laget over, fordi
maskinerne er lukkede — det eneste mulige er at genskabe deres hjemlige omgivelser.

Kæden: **dit program → libhybris → Android-maskinerne → pvrsrvkm (kernens dør) →
GPU'en**. Der kører altså ikke "et helt Android" — kun de dele af hallen, maskinerne
ikke kan undvære.

**Hvordan fungerer broen helt konkret?**

Dit program kalder helt almindelige GLES-funktioner — `eglGetDisplay` ("åbn
skærmen"), `glDrawArrays` ("tegn trekanten"). Udefra ligner de normale biblioteker
(`libEGL`/`libGLESv2`), men indeni er de **oversætter-udgaver** fra libhybris. De
gør tre ting:

1. **De læsser maskinerne ind.** Android-maskinerne er lukkede programmer, bygget
   til at stå i Android-hallen (de taler bionic, ikke Linux' C-bibliotek). Hybris
   har en indbygget Android-tolk — en lille dynamisk linker — der kan læsse dem ind
   i vores Linux-proces og finde deres bionic-afhængigheder, uden at der kører et
   helt Android. Det er derfor `system.img` bare kan ligge som en fil: vi stiller et
   stykke af hallen ind i vores bygning.
2. **De oversætter opkaldene.** Når dit program kalder en GLES-funktion, viderestiller
   oversætteren den til fabrikkens rigtige program gennem en **telefonbog**
   (funktionstabeller): Android-maskinerne lægger lister af adresser ud, og hybris
   fylder de pladser ud, som Linux-siden ejer, og slår resten op hos fabrikken.
   (Kode-42-historien ovenfor var præcis én plads i telefonbogen, der aldrig blev
   udfyldt — et tomt hul.)
3. **De afleverer billedet.** Når fabrikken har tegnet, ligger billedet i hallens
   lagerhal — Androids hukommelsesstyring **gralloc/ION**. Herfra kan vi hente det
   ud på to måder: bede fabrikken læse det tilbage i CPU-hukommelse
   (`glReadPixels`), eller lade en "platform" præsentere lagerhalens blok direkte på
   en skærm.

Et lille kort over kæden:

```
dit program (fx Kodi eller vores testklient)
   │  kalder EGL/GLES — ser normale ud
   ▼
libhybris' oversættere (libEGL/libGLESv2 + en eglplatform-"platform")
   │  læsser + oversætter til Android-maskinernes sprog
   ▼
Android-maskinerne i /system (+ /system/vendor):
   bionic, logd, servicemanager, gralloc/ION, PowerVR-blobs
   │  tegner i lagerhalen (gralloc-buffere)
   ▼
pvrsrvkm (kernens dør) → PowerVR G6110 (fabrikken)
```

**Hvad er en "EGL-platform"?** EGL er den dør, GLES-programmer går ind ad.
"Platformen" er den del af døren, der ved, hvordan et færdigt billede skal vises på
en bestemt slags skærm. Android havde platforme til sine egne skærme
(hwcomposer/surfaceflinger), og der fandtes en til Linux' framebuffer (fbdev) og en
til "ingen skærm" (null) — men der manglede én til **X-vinduer**. Det er den, vi til
slut byggede: `eglplatform_x11`.

### Daemonen døde med kode 42 — et tomt hul i telefonbogen

Da vi byggede GLES-daemonen, virkede alting lige indtil den første rigtige
tegne-kommando — så døde den helt uden forklaring. Det lignede ikke et normalt
nedbrud: boksen kørte først en lille "display-dans" (skifter kanal, slukker og
tænder skærmen) og lukkede derefter pænt ned med kode 42.

Gåden var, at den gjorde det helt bevidst. Fabrikkens vagt (en signal-fælde i
Android-laget) fanger programmer, der er ved at falde, rydder op — derfor dansen —
og lukker med en fast kode i stedet for at efterlade rod. Så spørgsmålet var ikke
"hvordan døde den", men "hvad fik vagten til at gribe ind?".

Svaret lå i telefonbogen. Android-maskinerne kalder hinanden gennem en slags
telefonbog (wrapperens funktionstabeller), og én plads i bogen — "læs pixels"
(`glReadPixels`) — var aldrig blevet udfyldt. Da daemonen ringede op, hoppede
programmet ud i tomrummet (adresse 0) og væltede; vagten fangede faldet.

Vi fandt det ved at følge sporene i rækkefølge: strace (aflytning af alle
systemkald) viste dansen og kode 42, gdb (stoppet i faldøjeblikket) viste at
programmet kaldte adresse 0, og til sidst læste vi selve telefonbogens tegninger
(disassembly) og så den tomme plads.

Løsningen var lille: ved opstart slår daemonen selv den rigtige adresse op i
fabrikkens rigtige telefonbog og skriver den ind i den tomme plads. Siden da kan
programmet "læse pixels" fra GPU'en — og daemonen kan hente sine billeder ud til
skærmen. (Teknisk historie: DOK §5.15a; opslagsværket: HAANDBOG fælde 18.)

### Fabrikken maler ind i et vindue — eglplatform_x11

Efter daemonen kunne hente billeder ud, stillede vi det næste spørgsmål: kan vi få
fabrikkens billeder ind i et **X-vindue, mens skrivebordet kører**? Det er præcis
det, en browser skal kunne for at vise WebGL.

Først byggede vi en hurtig demo: daemonen tegnede billedet, sendte pixels gennem en
socket, og et Python-program viste dem i et vindue. Det virkede (10 billeder i
sekundet) — men det var en omvej. Kodi og en browser kalder EGL direkte og beder
ikke om pixels gennem en socket. De har brug for en rigtig **platform**: en
oversætter, der selv henter billedet fra lagerhalen og sætter det ind i vinduet.

Så vi byggede `eglplatform_x11` — og det gav to gåder, der hver kostede en god del
af en aften. Begge var værd at forstå:

**Gåde 1: vinduet var der, men lærredet var tomt.** Programmet åbnede et vindue, X
sagde "vinduet er synligt" — men på TV'et var der bare skrivebord. Svaret lå i en
enkelt detalje: når fabrikken starter, **skifter den kanal på skærmen** (et
VT-skift i Android-laget). Og X maler kun ud til TV'et, når X' egen kanal er
aktiv. Vi tegnede altså ind i et lærred, der ikke var koblet til TV'et — i timevis,
fordi alle undersøgelser sagde "alt er i orden". Kur: efter fabrikkens startdans
skifter platformen kanalen tilbage til X' kanal ved det første billede. Siden da
sker det automatisk.

**Gåde 2: maleren stod i et andet rum.** Selv efter kanal-fixet var vinduet tomt,
når tegningen kom fra platformens egen X-forbindelse. Det viste sig, at denne
server kun tegner, når tegningen kommer fra **den samme forbindelse, der oprettede
vinduet** — som en maler, der kun må male i det rum, hvor han selv står. Kur:
klienten giver platformen sin egen X-forbindelse (som "native display"), så alle
tegninger går gennem den.

Undervejs blev vi snydt af en tilfældighed: vi troede et øjeblik, at en lille
lappegrej (LD_PRELOAD) ødelagde tegningen — men det var bare to forskellige
tilstande af gåde 1, der tilfældigt fulgtes ad. Læren står fast: **mål kanalen
(VT) først**, når noget ikke kommer på skærmen.

Resultatet i dag: et 640x360-vindue viser fabrikkens cos-mønster på TV'et — ~9
billeder i sekundet — mens skrivebordet kører. (Teknisk historie: DOK §5.15b;
fælderne: HAANDBOG fælde 19-22; koden: `devuan/gpu/eglplatform_x11/`.)

### Firefox' synsprøve er bestået — nu kæmper vi med selve brillerne

Med `eglplatform_x11` kunne vi stille det næste spørgsmål: vil Firefox bruge
den? Svaret blev "næsten". Da vi startede Firefox med `MOZ_X11_EGL=1` og vores
miljø, indlæste den vores EGL og hele Android-kæden — men dens indgangsprobe
(`glxtest`) spurgte "giv mig en skærm" (`eglGetDisplay`) og fik "ukendt skærm"
(EGL_BAD_DISPLAY), så Firefox trak sig tilbage til software-malingen.

Det mærkelige var, at det præcis samme spørgsmål virkede i vores egne små
programmer. Vi endte med at finde den rigtige forklaring i Firefox' egen kilde:
proben henter sine kerne-funktioner ad en anden vej end vores programmer
(gennem "find enhver funktion"-opkaldet `eglGetProcAddress` i stedet for
telefonbogen). Ad den vej ramte den Android-maskinernes egen dør — som kun
forstår Android-skærme — i stedet for vores oversætter, der kender X-vinduer.

Kuren var at lære vores platform at svare rigtigt, når Firefox spørger ad den
vej: platformen videresender nu kerne-funktionerne til oversætteren selv (og
byder på to tomme svar, som proben kræver findes). Derudover ville proben
opgive på forhånd, fordi boksens skærm er 16-bit og proben kræver 24-bit —
vi rettede den ene sammenligning i selve proben (med backup).

**Nu består Firefox' synsprøve:** proben melder `PowerVR Rogue G6110`, GLES
3.1, "EGL" — fabrikken er fundet! Men når hele Firefox starter, kan den stadig
ikke få selve tegnemaskinen (WebRender) i gang: oprettelsen af en GPU-kontekst
fejler på to målbare måder, og Firefox falder tilbage til software-WebRender.
Vi har sporet begge fejl med gdb og ved præcis, hvor vi skal kigge næste gang.
(Teknisk: DOK §5.15c; fælder: HAANDBOG 23 + to nye spor i
`devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md`; værktøjer:
`devuan/gpu/eglplatform_x11/`.)

### Spillet kører — men skærmen tier stille (25. aug 2026)

Så fik vi WebGL i Firefox til at virke — hele brugerfladen inklusive. Men da
vi prøvede et rigtigt spil (Subway Surfers på poki.com), skete det samme hver
gang: spillets første billede dukkede op på skærmen, og så stod ALT stille.
Firefox var ellers travl som en myretue — processorerne arbejdede, loggene
voksede — men skærmen ændrede ikke en pixel.

Vi fandt to forskellige måder at tegne Firefox-vinduet på, og prøvede dem
begge:

- **Basic-måden** (UI'et males af processoren direkte på skærmen): hele
  vinduet ser rigtigt ud, men spillet fryser efter første billede — Firefox
  sidder i en travl venteløkke der aldrig bliver færdig.
- **GL-layers-måden** (GPU'en maler, og vi flytter billedet ind i vinduet):
  nu ANIMERER spillet faktisk — vindue-pixlerne danser, billede-tælleren
  vokser. Men når vi kigger på selve skærmen, er den stadig frossen i det
  første billede. Og efter nogle minutter dør hele boksen (to gange målt —
  strømmen skal tages og gives igen).

For at være sikre på, at det ikke er skærm-driveren der er skyld i det, byggede
vi nogle helt små testprogrammer: de laver et vindue med samme "gennemsigtige"
farve-format som Firefox bruger, og maler skiftevis grøn og blå i det. Det
virker perfekt — skærmen skifter farve billede for billede, også når et andet
program laver malingen. Så X-serveren kan godt; fejlen ligger et sted i
samspillet mellem Firefox og GPU-processen, når den kører et rigtigt spil i
flere sekunder (eller genstarter sig selv midt i det hele — det gør den
indimellem, og så holder skærmen helt op med at opdatere).

Næste skridt: en **lokal stress-side uden reklamer og uden spilmotor** (bare
en animeret WebGL-verden), så vi kan se om skærmen også tier dér — hvis den
gør, er fejlen vores egen præsentations-sti; hvis ikke, er det noget særligt
ved poki/Unity-spillet. Vi prøver også at blokere reklamerne (uBlock) som
kontrol — de er ikke årsagen, men de slider på en i forvejen presset boks.
(Teknisk: DOK §5.15d; hele måle-forløbet:
`devuan/gpu/GL-LAYERS-SESSION-NOTAT-2026-08-25.md`.)

## 4. Hvad kan vi nu — og hvad kan vi ikke?

**Det vi kan:**
- Skrive vores **egne programmer**, der bruger GPU'en: 3D-trekanten kører — og
  almindelig GLES 3.1-programmering er nu åben. (Opskriften og programmet:
  `devuan/gpu/`, DOK §5.15)
- Vække stakken med én kommando (`gpu_up.sh`) og teste, måle og lege med den.
- Bygge et lille grafisk system: en Python-frontend der tegner sin egen UI direkte på
  `/dev/fb0` (som `fb_overscan.py` gør) og taler med en GLES-daemon i baggrunden over
  en unix-socket — daemonen renderer offscreen og blitter billederne til skærmen.
- **Vise GLES-billeder i et X-vindue, mens skrivebordet kører.** Det var længe
  "næsten": fabrikken kunne regne offscreen, men ikke vise noget i et vindue. Nu kan
  den — `eglplatform_x11` henter billedet fra lagerhalen og sætter det ind i
  X-vinduet (~9 billeder i sekundet i 640x360). Den gamle fuldskærms-vej
  (hwcomposer/kiosk) kræver stadig, at X holder pause.
- Forklare præcis, hvorfor noget virker eller ikke virker — hver fælde er målt og
  skrevet ned, så intet behøver gættes igen.
- **Køre WebGL 2.0 i Firefox med hele brugerfladen synlig** — med
  Basic-kompositoren: siden og canvas tegnes korrekt på skærmen (PowerVR
  G6110, `WEBGL_RESULT OK`). Desktop-genvejen "Firefox WebGL" virker fra
  LXDE-sessionen.

**Det vi ikke kan (endnu):**
- **WebGL-spil i browseren.** Med Basic-kompositoren fryser spillet efter
  første billede (en travl venteløkke i præsentationsstien). Med
  GL-layers-kompositoren animerer spillet i vinduet, men skærmen opdaterer
  ikke — og spil-kørsler har taget boksen ned to gange. Næste skridt står i
  DOK §5.15d og `devuan/gpu/GL-LAYERS-SESSION-NOTAT-2026-08-25.md`.

**Reglerne vi lærte (kort):**
1. En GPU-stak er tre lag — og mangler ét, virker intet.
2. Mål altid på den rigtige boks — mærkater lyver, også strømforsyningers.
3. Gammel hardware er som en skattejagt: det meste af løsningen lå allerede i
   producentens efterladte filer — man skal bare vide, hvad man leder efter.

## 5. Hvor man læser mere

| Vil du vide mere om... | Læs |
|---|---|
| Historien om hele Devuan-projektet og alle beslutningerne | `DOKUMENTATION.md` |
| Grafikken i tekniske detaljer (målinger, fejlsøgning, opskrift) | `DOKUMENTATION.md` §5.13-5.15 |
| Fælderne, skrevet som opslagsværk med symptom → årsag → kur | `HAANDBOG.md` (især fælde 16-22) |
| Hvorfor vi ikke bare kan bruge en ny kerne | `DRIVER-PORTERING.md` |
| Hvordan GPU'en kan komme ind i en browser — løsningsanalyse og rækkefølge | `BROWSER-VEJE.md` |
| Vores GPU-programmer og diagnose-værktøjer | `devuan/gpu/` (og `devuan/gpu/diagnostik/`, `devuan/gpu/eglplatform_x11/`) |
| Hele rejsens historie i git | `git log` — hver commit fortæller et kapitel |
