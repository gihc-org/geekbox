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

## 4. Hvad kan vi nu — og hvad kan vi ikke?

**Det vi kan:**
- Skrive vores **egne programmer**, der bruger GPU'en: 3D-trekanten kører — og
  almindelig GLES 3.1-programmering er nu åben. (Opskriften og programmet:
  `devuan/gpu/`, DOK §5.15)
- Vække stakken med én kommando (`gpu_up.sh`) og teste, måle og lege med den.
- Bygge et lille grafisk system: en Python-frontend der tegner sin egen UI direkte på
  `/dev/fb0` (som `fb_overscan.py` gør) og taler med en GLES-daemon i baggrunden over
  en unix-socket — daemonen renderer offscreen og blitter billederne til skærmen.
- Forklare præcis, hvorfor noget virker eller ikke virker — hver fælde er målt og
  skrevet ned, så intet behøver gættes igen.

**Det vi ikke kan (endnu):**
- **WebGL-spil i browseren.** Browseren skal bruge den moderne transportvej
  (KMS/DRI), som vores 2013-kerne ikke har. Man skulle bygge en ny oversætter til
  browseren — et stort projekt (uger-måneder), selvom fabrikken nu kører.
- **Skrivebord og GPU samtidig — næsten.** Konflikten handler kun om SKÆRMEN (et
  lærred, to malere): GLES kan sagtens regne og tegne offscreen, mens X kører — men
  intet må vise noget på TV'et samtidig med X. Skal GPU'en vise noget, må X holde
  pause — og efter en GPU-session skal boksen strøm-cykles for at få HDMI tilbage.
  En baggrundsproces kan sagtens eje skærmen alene: render med GLES til en
  offscreen-buffer, og "pip" resultatet til VOP'en ved at skrive til `/dev/fb0`.

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
| Fælderne, skrevet som opslagsværk med symptom → årsag → kur | `HAANDBOG.md` (især fælde 16-17) |
| Hvorfor vi ikke bare kan bruge en ny kerne | `DRIVER-PORTERING.md` |
| Vores GPU-programmer og diagnose-værktøjer | `devuan/gpu/` (og `devuan/gpu/diagnostik/`) |
| Hele rejsens historie i git | `git log` — hver commit fortæller et kapitel |
