# Browser-vejene: hvordan GPU'en kommer ind i en browser

Spørgsmålet der startede dokumentet: *"Vil det være en uoverkommelig opgave at forke
Firefox og få den til at tale med vores GPU-stak?"* Svaret er nej — men arbejdet
ligger næsten alle andre steder end i Firefox' egen kode. Dette dokument samler
løsningen, risikoerne og en foreslået rækkefølge (aug 2026). Grundlaget er DOK §5.15,
`GRAFIK-FORKLARET.md` og operationskortet `devuan/gpu/README.md`.

## 1. Grundlaget: hvad broen kan og ikke kan (målt i repoet)

- Vendors hybris-bro indeholder fire EGL-platforme: `eglplatform_null`,
  `eglplatform_fbdev`, `eglplatform_hwcomposer`, `eglplatform_surfaceflinger`
  (`dualos_blobs/box_tree/opt_hybris/libhybris/`). Der er **ingen `eglplatform_x11`**.
- Producentens egen opsætning brugte `EGL_PLATFORM=hwcomposer`
  (`vendor_root/etc/profile.d/libhybris_path_eglplatform.sh`) — altså direkte til
  skærmen, ikke ind i X.
- Producenten leverede både `kodi.bin` og en chromium 47 med GLES2-ozone-lag
  (`vendor_root/usr/lib/kodi/`, `vendor_root/usr/lib/chromium-browser/`) — de har i
  2016 kørt både en medie-app og en browser mod stakken. Hvordan chromium'en
  præsenterede (hvilken ozone-platform, X eller ej), har vi ikke målt.
- Konsekvens for en browser: den *skal* køre under X (vinduer, mus, tastatur), men
  ingen af de fire platforme kan levere GPU-billeder ind i et X-vindue. De taler
  direkte til skærmen — præcis den kollision med X vi allerede har målt
  (`devuan/gpu/README.md` regel 1; strøm-cyklus bagefter, regel 2).

## 2. Løsningen i fire dele

### A. `eglplatform_x11` til hybris — brostykket (forudsætningen)

Mønsteret er velkendt: PVR'en renderer offscreen, og resultatet blittes ind i
X-vinduet via XPutImage/MIT-SHM. Det er samme form som det planlagte
GLES-daemon-projekt (TODO.md) — bare ind i et X-vindue i stedet for `/dev/fb0`.
Ældre libhybris har haft en sådan platform, så formen findes.

Størrelse: et par hundrede linjer C + debugg. Afhænger ikke af Firefox — stykket
gavner Kodi og vores egne programmer lige så meget. Båndbredden er til at leve med:
1920 × 1080 × 4 byte ≈ 8 MB pr. frame som ren memcpy; den CPU-skrevede X-server
(fbdev) laver ikke andet end memcpy i forvejen.

### B. Firefox-siden

Firefox kan EGL/GLES i forvejen — det er sådan den kører på Android — og
`MOZ_X11_EGL=1` findes allerede i stock-Firefox. Det der sandsynligvis skal til:
PowerVR fjernet fra blocklisten, WebRender-GLES-stien tvunget til, måske små patches
i `GLContextProviderEGL`. En build af Firefox kræver en PC (boksens 2 GB rækker
ikke), timer og 50+ GB disk.

### C. Det uafklarede: 2016-blob'en mod Firefox' krav

Om PVR-driverens GLES 3.1 har de extensioner og den stabilitet, WebRender kræver,
kan ingen vide, før det er prøvet. Det er her risikoen bor — men fejltilstanden er
"langsomt/buggy", ikke "umuligt".

### D. Vedligeholdelse — den løbende pris

En browser-fork skal rebases mod sikkerhedsrettelser for evigt, og det er en
browser. Derfor: hold dig så tæt på stock-Firefox som muligt (env-vars + små
patches) frem for en hård fork.

## 3. Fire billige eksperimenter før nogen fork (~en aften hver)

1. **Læs vendors egen chromium-opsætning** (`vendor_root/etc/chromium-browser/`,
   ozone-libs'ene) — måske står svaret på, hvordan en GLES-browser præsenterede på
   netop denne boks i 2016, allerede der.
2. **Stock Firefox med `MOZ_X11_EGL=1`** + `LD_LIBRARY_PATH=/opt/hybris` (+ shim).
   Den vil næppe virke (manglende x11-platform), men loggen viser præcis, hvad
   Firefox kræver af EGL'en. Advarsel: rører forsøget hwc-init på skærmen, gælder
   regel 1+2 — stop X eller forvent strøm-cyklus bagefter.
3. **Prototype af A:** vis et PVR-renderet billede i et X-vindue, mens X kører.
   Virker det, er den store tekniske risiko afklaret — og stykket kan bruges af alle.
4. **Kodi fra Debian-armhf** + broerne i `/opt/hybris`: producenten kørte netop
   denne kombination i 2016. Den korteste vej til en GPU-accelereret fuldskærms-app,
   og samtidig beviset for at stakken kan bære en rigtig app.

## 4. Foreslået rækkefølge

1. Byg `eglplatform_x11` (A) som selvstændigt projekt — det er forudsætningen,
   uanset hvilken vej man ender på, og det gavner alle brugere af stakken.
2. Kør eksperiment 3 (prototype i X-vindue) — måling i stedet for gæt.
3. Kodi (eksperiment 4) — nyttiggør vendor-kodi-binæren (TODO trin 2).
4. Først derefter: beslutningen om browseren — som til den tid måske viser sig at
   hedde "stock Firefox + MOZ_X11_EGL + en lille patch" frem for en egentlig fork.
5. I mellemtiden: `chromium` + SwiftShader er den eneste browser-vej med WebGL, der
   virker i dag (HAANDBOG fælde 16) — CPU-fart, men den virker.

## 5. Dommen

Ikke uoverkommeligt — men "uger-måneder" (`GRAFIK-FORKLARET.md` §4) holder, og
arbejdet ligger næsten alle andre steder end i Firefox' egen kode. Risikoen samler
sig i to punkter: A (kendt form, afgrænset) og C (uafklaret indtil målt —
eksperiment 3 afgør den). Målet "webspil i dag" → chromium+SwiftShader. Målet
"GPU-nyttiggørelse" → eglplatform_x11 + Kodi/egne programmer først.

## 6. Hvor man læser mere

| Vil du vide mere om... | Læs |
|---|---|
| GPU-stakken, opskriften og reglerne | `devuan/gpu/README.md`, DOK §5.15 |
| Historien i hverdagssprog | `GRAFIK-FORKLARET.md` |
| WebGL-fælden og chromium-nødløsningen | `HAANDBOG.md` fælde 16, DOK §5.13-5.14 |
| Planen i kort form | `TODO.md` (trin 2 og Python/GLES-daemon-projektet) |
