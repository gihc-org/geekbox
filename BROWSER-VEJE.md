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
  2016 kørt både en medie-app og en browser mod stakken. Målt (aug 2026): chromium'en
  er en GTK2+X11-build (`libgtk2ui.so`, `libx11_events_platform.so` i dens `libs/`),
  så den kørte under X — og dens wrapper starter med `--no-sandbox` (samme
  syscall-problem som HAANDBOG fælde 14). Stadig umålt: hvilken GL-vej den brugte
  (`libgl_wrapper.so` = GLX vs. `libgles2_c_lib.so` = GLES2) og hvilken ozone-platform
  der præsenterede — se måleprogrammet i §3.
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

## 3. Måleprogrammet: hvad der kan måles inden vi bygger noget

Alt nedenstående kan gøres uden at bygge løsningen — i projektets ånd: målt, ikke
gættet. Nummereringen er gruppe + punkt, så den ikke forveksles med eksperimenterne
i §4.

### A. Repo-analyser (uden boks)

A1. **Vendors chromium-ledninger.** `readelf -d` på `chromium-browser` og på
    `libgl_wrapper.so`/`libgles2_c_lib.so` — hvilken libEGL/libGLESv2 de linker og
    importerer (vendors hybris i `/usr/local/lib` eller mesa?). `strings | grep -i
    ozone` på libs'ene for platformnavne (`egltest`/`x11`/`headless`).
    `vendor_root/usr/local/lib/pkgconfig/` og `.la`-filerne viser, hvordan EGL'erne
    var tænkt linket.
A2. **Vendors kodi-ledninger.** Samme behandling af `kodi.bin` + `kodi-config.cmake`
    (byggeflag afslører ofte EGL-backenden: `EGLPLATFORM=...`). Facit på, hvordan en
    rigtig GLES-app præsenterede i 2016.
A3. **EGL-platform-API-kontrakten** (de-risker opgave A i §2 direkte):
    `vendor_root/usr/local/include/hybris/eglplatformcommon/eglplatformcommon.h`
    definerer præcis, hvilke funktioner en ny platform skal implementere — suppleret
    med `nm -D` på `eglplatform_hwcomposer.so` (samme funktionssæt = facitlisten for
    `eglplatform_x11`). A bliver fra "et par hundrede linjer" til en konkret
    tjekliste.
A4. **Hybris-EGL'ens overflade.** `objdump -T` + `strings` på vendors `libEGL.so`:
    hvilke EGL-entry-points og client-extensioner den reklamerer med
    (`EGL_EXT_platform_base`, `EGL_KHR_surfaceless_context` …). Det er præcis den
    liste, en browser spørger om ved init.
A5. **PVR-blob'ens statiske capability-profil.** `strings` på
    `libGLESv2`/`libEGL_POWERVR_ROGUE.so` (`dualos_blobs/system_lib`,
    `dualos_blobs/vendor_lib`) — GL-extensionerne står næsten altid som tekst i
    binæren. Første statiske liste at holde op mod WebRenders krav (risiko C i §2)
    uden at røre boksen.
A6. **Browsernes kravkatalog** (webforskning). Mozilla-kildens GLES-krav og
    PowerVR-blocklist-kriterier (searchfox), og hvad ozone-GLES2 krævede i
    chromium 47-æraen. Sammenlignes med A4+A5 → kvalificeret gæt om C før
    eksperimenterne.

### B. Boks-målinger (sikre — X kan køre imens)

B7. **Null-platform-probe — den vigtigste, og den er sikker.** `eglplatform_null.so`
    findes i broen, og reglen i `devuan/gpu/README.md` siger, at GLES offscreen kan
    køre mens X kører. Et lille program (udvidelse af `test_triangle.cpp`) laver med
    `EGL_PLATFORM=null` en offscreen-kontekst og dumper: fuld EGL+GLES-
    extensionliste, GLES 3.0/3.1-feature-bits, og forsøger præcis de features
    WebRender skal bruge (instancing, UBO std140, sRGB, float-texturer, blit, MSAA)
    plus en lille perf-loop. Måler risiko C ved kørsel — uden at røre skærmen, uden
    strøm-cyklus.
B8. **X-blit-båndbredde.** Ren X-måling: 1920×1080 XPutImage/MIT-SHM ind i et vindue
    i en loop. Validerer A's præsentationstese (8 MB/frame memcpy) uden GPU og uden
    risiko.
B9. Derefter `MOZ_X11_EGL`-forsøget (eksperiment 2 i §4) — nu med en forudsigelse fra
    A4+A5+A6, så loggen kan læses mod forventningen i stedet for at tolkes frit.

### C. Forskning (web)

C10. Gammel libhybris-`eglplatform_x11`-kilde (GitHub-historien) — bekræfter form og
     licens, og gør A's estimat eksakt.
C11. Debian-armhfs nuværende kodi's EGL-understøttelse — hvilke platforme kan den
     bruge her?

### Hvad man ikke skal gøre tidligt

**Køre vendors chromium 47.** Bygget til 14.04-æraens glibc, et sikkerhedshul på
ben, og den fortæller ved kørsel mindre end den statiske analyse i A1. Undtagelse:
kun hvis A1 peger på noget overraskende, der kun kan afgøres dynamisk.

Rækkefølgen der giver mest per time: **A5+B7** (C-risikoen — den store ubekendte),
**A3** (A-risikoen), **B8** (præsentationstesen), **A1+A2** (vendors facit), så
A6/C10/C11 — og derefter eksperimenterne i næste afsnit.

## 4. Fire billige eksperimenter før nogen fork (~en aften hver)

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

## 5. Foreslået rækkefølge

1. Byg `eglplatform_x11` (A) som selvstændigt projekt — det er forudsætningen,
   uanset hvilken vej man ender på, og det gavner alle brugere af stakken.
2. Kør eksperiment 3 (prototype i X-vindue) — måling i stedet for gæt.
3. Kodi (eksperiment 4) — nyttiggør vendor-kodi-binæren (TODO trin 2).
4. Først derefter: beslutningen om browseren — som til den tid måske viser sig at
   hedde "stock Firefox + MOZ_X11_EGL + en lille patch" frem for en egentlig fork.
5. I mellemtiden: `chromium` + SwiftShader er den eneste browser-vej med WebGL, der
   virker i dag (HAANDBOG fælde 16) — CPU-fart, men den virker.

## 6. Dommen

Ikke uoverkommeligt — men "uger-måneder" (`GRAFIK-FORKLARET.md` §4) holder, og
arbejdet ligger næsten alle andre steder end i Firefox' egen kode. Risikoen samler
sig i to punkter: A (kendt form, afgrænset) og C (uafklaret indtil målt —
eksperiment 3 afgør den). Målet "webspil i dag" → chromium+SwiftShader. Målet
"GPU-nyttiggørelse" → eglplatform_x11 + Kodi/egne programmer først.

## 7. Hvor man læser mere

| Vil du vide mere om... | Læs |
|---|---|
| GPU-stakken, opskriften og reglerne | `devuan/gpu/README.md`, DOK §5.15 |
| Historien i hverdagssprog | `GRAFIK-FORKLARET.md` |
| WebGL-fælden og chromium-nødløsningen | `HAANDBOG.md` fælde 16, DOK §5.13-5.14 |
| Planen i kort form | `TODO.md` (trin 2 og Python/GLES-daemon-projektet) |
