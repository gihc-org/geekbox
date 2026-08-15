# Hvorfor er det så svært at porte 3.10-vendordrivere til en mainline-kernel?

Dette dokument forklarer baggrunden for den vigtigste arkitektoniske beslutning i
projektet (se DOKUMENTATION.md §2: vendor-kernel + nyt userland). Det besvarer to
spørgsmål:

1. Hvad betyder det, at "Linux-kernens interne ABI ikke er stabil"?
2. Hvis `ndiswrapper` kunne køre *Windows*-WiFi-drivere på Linux — hvorfor er
   Linux → Linux så meget sværere?

Svaret på nr. 2 er det interessante, for det afslører hvad problemet egentlig er.

---

## 1. Tre lag, ikke ét

Når folk siger "kernens interne ABI er ikke stabil", blander de tre ting sammen.

### ABI — binær kompatibilitet

Struct-layout. `struct sk_buff` har ikke de samme felter i samme rækkefølge i 3.10
og 6.12. En prækompileret `.ko` fra 3.10 læser felt nr. 7 på offset 48 — i en ny
kernel ligger noget helt andet der.

Kernen *tjekker* faktisk for dette:

- **`vermagic`** — en streng med kerneversion + SMP + PREEMPT + compiler-ABI, som
  skal matche præcist.
- **`modversions`** — en CRC-checksum over hver eksporteret funktions typesignatur.

Bemærk hvad det er: **Linux har ABI-*detektion*, Windows har ABI-*garanti*.** Det er
en fundamental forskel i designfilosofi, ikke en implementeringsdetalje.

### API — kildekode-kompatibilitet

Selv *med* kildekoden skifter signaturer konstant. `file_operations`-medlemmer får og
mister argumenter, `ioremap_nocache` forsvinder, `probe`-signaturen ændres,
`struct device_driver` omorganiseres. Det er irriterende, men mekanisk arbejde.

### Semantik og kontrakter — her ligger det svære

Hvor det stadig *hedder* det samme, er **reglerne** ændret:

- låsningsregler (BKL væk, RCU overalt, mutex vs. semaphore)
- hvornår man må sove, og i hvilken kontekst
- memory-barrier-krav
- DMA-cache-cohærens og IOMMU
- hvem der ejer et objekts refcount

En 3.10-driver kan kompilere mod en ny kernel efter mekanisk fiksning — og så deadlocke
eller korrumpere hukommelse i uge tre, fordi den holder et lock forkert. Det er den
slags fejl, der ikke findes ved at læse compiler-warnings.

### Hvorfor kernefolkene vil det sådan

Se `Documentation/process/stable-api-nonsense.rst` i kernetræet. Kort fortalt: en
frossen intern ABI ville låse dårlige designs fast for evigt og gøre sikkerheds- og
performancearbejde umuligt. Prisen betales af out-of-tree-kode — og det er efter deres
mening netop et argument for at komme in-tree.

---

## 2. Hvorfor ndiswrapper kunne virke

ndiswrapper var muligt, fordi fem forudsætninger var opfyldt **samtidigt**:

| Forudsætning | Hvorfor det gjaldt for NDIS |
|---|---|
| Der fandtes en specifikation at emulere | NDIS er dokumenteret, versioneret og *bevidst stabil* — Microsoft skal kunne køre tredjeparts binære drivere på tværs af Windows-versioner, så de har bundet sig kontraktligt. Der er noget at implementere *op imod*. |
| Grænsefladen var smal | En NDIS miniport-driver registrerer en håndfuld callbacks (`MiniportInitialize`, `MiniportSend`, OID-baseret query/set) og kalder et par hundrede kernefunktioner: alloker hukommelse, tag et spinlock, map DMA, læs PCI config space, sæt en timer. Overkommeligt at genskabe. |
| Grænsefladen var *abstrakt* | NDIS skjuler bevidst Windows-kernens indmad. Driveren rører ikke Windows' interne datastrukturer direkte. |
| Domænet var snævert | "Pakker ind, pakker ud, plus lidt konfiguration." Der findes en oplagt oversættelse til Linux' `net_device`, fordi begge sider modellerer samme velafgrænsede virkelighed. |
| Hardwaren var selvbeskrivende og diskret | Et PCI- eller USB-kort på en bus, der kan enumereres, med sin egen strømforsyning og clock ombord. |

---

## 3. Hvorfor det ikke oversættes til RK3368

Vend nu hver af de fem forudsætninger om.

### Der er ingen specifikation

"3.10-driver-API'et" er ikke en kontrakt — det **er** 3.10's kildekode. Der findes ikke
et dokument at implementere. En shim skulle genskabe *adfærd*, ikke overholde en
grænseflade.

### Grænsefladen er ikke smal, og det meste af den er ikke engang symboler

Enormt meget af hvad en Linux-driver "kalder", er makroer og `static inline`-funktioner,
som compileren har **bagt ind i driveren**. Driveren har hardkodet kernens interne
layout og logik i sit eget tekstsegment. Der er intet call-site at fange.

I NDIS går alting gennem en rigtig eksporteret funktion i en versioneret import-tabel.

### Der er ingen abstraktion

En Linux-driver **er** kernen. Den læser og skriver kernens globale datastrukturer
direkte, tager kernens låse, indgår i dens memory-model, bruger `current`, pakker
`struct page` ud. Der er ingen indkapsling at shimme ved, fordi in-tree-kode ikke
behøver nogen — man retter bare alle kaldere samtidigt.

### Domænet er ikke snævert — den vigtigste pointe for denne boks

Du har ikke ét netkort. Du har clock-tree, power domains, pinmux, regulatorer,
DDR-controller, thermal, eMMC, USB-PHY, HDMI-PHY, VPU og PowerVR SGX6110-GPU'en. Det er
**on-chip-blokke uden nogen bus, der kan enumerere dem**, og de er uadskillelige fra
platform-koden. Der er ikke noget lag at shimme *ved*.

Og de skal tale sammen: display-driveren skal dele en buffer med GPU-driveren gennem
kernens dma-buf-objektmodel og hente sin clock fra clk-frameworket. To shim'ede drivere
kan ikke dele et dma-buf, medmindre shimmen genskaber hele kernens objektmodel — hvilket
er det samme som at genskabe kernen.

### Subsystemerne er ikke porteret, de er *erstattet*

3.10-grafikken her er fbdev plus Rockchips egne ioctls. Mainline er DRM/KMS med atomic
modesetting og GEM. Det er ikke en oversættelse, det er et andet design med en anden
brugerflade opad. Samme historie for V4L2, ASoC, pinctrl og thermal. Og vendor-DT'ens
bindings i 3.10 er ikke mainline-bindings.

### Oveni: en vendor-BSP er ikke "rent træ + moduler"

Rockchip patchede kernens egne filer. Det vi har, er en **fork af Linux 3.10**, ikke en
samling moduler, der kan flyttes. Og GPU-delen har en brugerland-blob, bygget mod præcis
den kernel-driverens ioctl-ABI — så selv en perfekt kernel-shim efterlader os med en
blob, der ikke kan tale med den.

---

## 4. Modellen findes faktisk — og prisen viser problemets størrelse

Der *er* folk, der gør varianter af dette. De er lærerige:

- **Nvidias proprietære driver** — portabel binær kerne + tynd open source "glue layer",
  der genkompileres per kernel. Det virker, fordi Nvidia designede for det fra dag ét —
  og det brækker stadig rutinemæssigt ved nye releases.
- **RHEL/SUSE kABI** — en whitelist af symboler med frosne signaturer, holdt stabil
  inden for én major-release. Kræver et betalt fuldtidshold, og kun inden for én version.
- **`backports`-projektet** (tidligere compat-wireless) — shimmer *nye* mainline-drivere
  ned på *gamle* kerner. Bemærk retningen: det er den, der virker, fordi kildekoden er
  velopdragen og kan følges med. Ingen laver den modsatte vej.
- **VFIO/UIO** — flyt driveren til userspace bag et stabilt uapi. Den rigtige flugtvej,
  men kræver en driver skrevet for det, og en IOMMU.

---

## 5. Hvad det betyder for dette projekt

Løsningen — behold 3.10.79 med de fungerende drivere og læg Devuan-userland ovenpå — er
den rigtige arkitektoniske indsigt: **vi shimmer ved den grænseflade, der faktisk *er*
stabil.**

Linux' syscall-ABI og uapi er nemlig garanteret bagudkompatibel, i modsætning til alt
det interne. Det er derfor Devuan-userland fra 2024 kan køre på en kernel fra 2015. Det
er det ene sted i hele stakken, hvor Linux giver et ndiswrapper-agtigt løfte.

Prisen står i resten af dokumentationen:

- ingen KMS → `xserver-xorg-legacy` + `allowed_users=anybody`
- `systemd-sysusers` fejler med EINVAL på 3.10 → divert'ed væk
- fbdev-race på bpp → `fbset`-normalisering i myinit

Alt det er **syscall-ABI'ens kanter**: nyt userland, der forventer kernefaciliteter fra
efter 2015. Det er en helt anden og meget mildere klasse af problem end at portere en
GPU-driver.

---

## Videre læsning

- `Documentation/process/stable-api-nonsense.rst` — kernens eget rationale
- `Documentation/driver-api/` — de nuværende driver-frameworks
- DOKUMENTATION.md §2 — hvorfor Spor A blev valgt
- TODO.md — Spor B (mainline), og hvorfor det er parkeret
