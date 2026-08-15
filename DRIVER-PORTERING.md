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
DDR-controller, thermal, eMMC, USB-PHY, HDMI-PHY, VPU og PowerVR G6110-GPU'en. Det er
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

## 6. Sikkerhed: er 3.10 stadig vedligeholdt, og kan man bygge en sikrere kerne?

### Nej — og vi er endda bagud inden for 3.10

Linux 3.10 nåede end-of-life **4. november 2017** med `3.10.108`. Willy Tarreau, der
vedligeholdt serien, erklærede den død efter 108 maintenance-releases. Siden da: ingen
CVE-fixes, ingen backports.

Men bemærk detaljen: **vores kerne er 3.10.79 fra 2015.** Der findes altså ~29
stable-releases, som *blev* lavet, og som vi ikke har — alt fra 3.10.80 til 3.10.108,
inklusive Dirty COW (CVE-2016-5195). Vi er ikke bare på en EOL-kerne; vi er tre år bagud
*inden for* den EOL-kerne.

Den eneste 3.10-linje, der stadig får opdateringer, er **RHEL 7's** kerne (Red Hats tungt
patchede fork, ELS til juni 2028). Men det er en x86-fokuseret fork med sin egen
patch-historie — at cherry-picke derfra til en Rockchip arm64-vendorfork er urealistisk.
Til sammenligning laver CIP super-lang vedligeholdelse professionelt, men deres ældste
træ er 4.4. 3.10 er under gulvet for alle.

### Det er en klippe, ikke en bakke

Her ligger den intuition, der er nem at gå glip af. "Nyere kerne" er **ikke** en glidende
skala, hvor mere nyt = mere arbejde:

| Fra → til | Intern ABI | Arbejde |
|---|---|---|
| 3.10.79 → **3.10.108** | **Stabil ved politik** | Dage. Driverne bygger nærmest uændret |
| 3.10 → 4.4 | Brudt | År. Reelt uoverkommeligt |

Grunden er stable-kernel-reglerne: en fix må kun optages i en stable-serie, hvis den
*ikke* ændrer interne API'er eller struct-layout. **Inden for `3.10.x` har Linux altså
præcis den stabile interne ABI, der ikke findes mellem major-versioner.** Det gør
3.10.79 → 3.10.108 til et weekendprojekt frem for et livsværk.

Kildekoden findes: [`geekboxzone/lollipop_kernel`](https://github.com/geekboxzone/lollipop_kernel)
(branch `geekbox`), med mirror hos
[`abhisit/rk3368-linux-3.10.79-lollipop-ubuntu`](https://github.com/abhisit/rk3368-linux-3.10.79-lollipop-ubuntu).
Forbehold: Rockchip patchede kernens egne filer, så en merge op til 3.10.108 giver
konflikter i `mm/` og `arch/arm64/` — men det er konflikter i kendt kode, ikke et redesign.

### Hvad "sikrere" realistisk kan betyde, sorteret efter udbytte pr. indsats

**a) Merge til 3.10.108.** Tre års stable-fixes gratis. Størst udbytte.

**b) Skær attack surface væk i `.config`.** Bedste indsats/effekt-ratio, og ingen
ABI-risiko. De klassiske local-privesc-CVE'er sidder i eksotiske protokolhandlere og
filsystemer, vi aldrig bruger: DCCP, SCTP, AppleTalk, IPX, mærkelige `fs/`-drivere, USB
gadget. Slå dem fra, så findes hullerne ikke. Plus `CONFIG_MODULE_SIG`, `kptr_restrict`,
`dmesg_restrict`, `CONFIG_STRICT_DEVMEM`, `CONFIG_DEVKMEM=n`, Yama LSM (findes siden 3.4).

**c) Slå den hardening til, der *findes* i 3.10** — og accepter, at det er skuffende lidt.
Næsten al kernel-hardening blev opfundet efter 3.10:

| Feature | Kom i |
|---|---|
| `HARDENED_USERCOPY` | 4.8 |
| `SLAB_FREELIST_RANDOM` | 4.7 |
| `FORTIFY_SOURCE` | 4.13 |
| arm64 KASLR | 4.6 |
| arm64 `STRICT_KERNEL_RWX` | ~4.11 |
| `REFCOUNT_FULL` | 4.11 |
| `STACKPROTECTOR_STRONG` | 3.14 (vi har kun almindelig stackprotector) |

Seccomp er en undtagelse værd at forstå: *native arm64* seccomp-filter kom først i 3.19,
men vores userland er **armhf**, og ARM32's seccomp-filter har eksisteret siden 3.5. Via
`CONFIG_COMPAT` har vi derfor seccomp — og det er netop derfor OpenSSH's sandbox kunne
dræbe preauth (se TODO.md, Spor A).

**d) Cherry-picke enkelte CVE-fixes efter 2017.** Muligt for en håndfuld, men hver fix
rører kode, der er refaktoreret siden — altså samme semantik-problematik som i §1, blot
i småt format.

### Realitetstjek på truslen

"Sikker" afhænger af trusselsmodel, og vores er bedre end kerneversionen antyder:

- **Det meste af attack surface er allerede moderne.** OpenSSL, TLS-stakken, alt der
  møder netværket i userland, er Devuan Excalibur og bliver patchet. Det er projektets
  reelle sikkerhedsgevinst, og den er stor.
- **De fleste kernel-CVE'er er local privilege escalation** — de kræver, at nogen allerede
  kan køre kode på boksen. Uden utroværdige lokale brugere er de andenordens.
- **Cortex-A53 er in-order** og står ikke på ARMs liste over kerner ramt af
  Meltdown/Spectre v2 (A15/A57/A72/A73/A75 gør). Den store 2018-panik gælder ikke denne
  chip. Omvendt logik: Dirty Pipe (CVE-2022-0847) blev introduceret i 5.8 og findes slet
  ikke her. Gammel ≠ sårbar over hele linjen.
- **Den reelle bekymring: BCM4354-WiFi'en.** Broadcom-firmware fra den æra havde
  remote-eksekverbare bugs (Broadpwn-familien), den parser fjendtlige frames før
  authentication, og firmwaren er en blob, der ikke kan patches. Skal boksen stå et
  utroværdigt sted, er kabel frem for WiFi en ægte mitigation.

### Hvorfor det hænger sammen med resten af dokumentet

Læg mærke til, at **begge** realistiske veje følger en søm:

1. **syscall/uapi-grænsen** — bagudkompatibel for evigt. Det er den, Spor A bruger til at
   sætte 2024-userland på en 2015-kernel.
2. **inden for en stable-serie** — frosset ved politik. Det er den, en sikkerhedsopdatering
   kan bruge til at komme fra 3.10.79 til 3.10.108.

Alt andet i kernen er bevidst sømløst. Det er hele forklaringen på, hvorfor mainline er
en mur, mens en sikkerhedsopdatering er et overkommeligt stykke arbejde.

---

## Videre læsning

- `Documentation/process/stable-api-nonsense.rst` — kernens eget rationale
- `Documentation/process/stable-kernel-rules.rst` — reglerne der gør `3.10.x` ABI-stabil
- `Documentation/driver-api/` — de nuværende driver-frameworks
- [Linux 3.10.108 (EOL) — LWN](https://lwn.net/Articles/738167/) — annonceringen
- [Willy Tarreau: Look back to an end-of-life LTS kernel: 3.10](http://wtarreau.blogspot.com/2017/11/look-back-to-end-of-life-lts-kernel-310.html)
- DOKUMENTATION.md §2 — hvorfor Spor A blev valgt
- TODO.md — Spor B (mainline), og hvorfor det er parkeret
