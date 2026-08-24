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
    **Målt (24. aug 2026):** `chromium-browser` er en GTK2+X11-build
    (NEEDED: `libgtk2ui.so`, `libgfx_x11.so`, `libx11_events_platform.so` +
    libX11/Xext/Xcomposite/Xrender/Xi/Xdamage/Xfixes). GL indlæses dynamisk via
    `libgl_wrapper.so`, som dlopen'er **både** `libGL.so.1` og `libEGL.so.1` +
    `libGLESv2.so.2` og indeholder både GLX-stier
    (`ui/gl/gl_context_glx.cc`, `gl_surface_glx.cc`) og hele EGL-fejltabellen.
    `libgles2_c_lib.so` er Chromiums in-process GLES2-commandbuffer.
    `dualos_blobs/system_lib/egl.cfg` = `0 0 POWERVR_ROGUE` (Android-loaderens
    drivervalg). GPU-driver-buglisten i `libgpu.so` indeholder PowerVR-Rogue-
    regler, men alle scoped til `os: android` (id 104: gpu_rasterization fra;
    id 76: `EGL_KHR_fence_sync` skruet ned ≤4.4.4; id 33: share-group →
    virtualiserede kontekster). Konklusion: 2016-chromium'en HAVDE begge GL-veje
    bygget ind; hvilken den valgte på boksen er et runtime-valg (GLX via libGL
    eller EGL/GLES2 via hybris).
A2. **Vendors kodi-ledninger.** Samme behandling af `kodi.bin` + `kodi-config.cmake`
    (byggeflag afslører ofte EGL-backenden: `EGLPLATFORM=...`). Facit på, hvordan en
    rigtig GLES-app præsenterede i 2016.
    **Målt (24. aug 2026):** `kodi.bin` linker DIREKTE mod `libGLESv2.so.2` +
    `libEGL.so.1` (hybris, ikke mesa), og `kodi-config.cmake` er bygget med
    `-DTARGET_HYBRIS -DBUILD_KODI_ADDON` (Kodi 15.2). Facit: en rigtig GLES-app i
    2016 hægtede sig på hybris' libEGL/libGLESv2 (med `EGL_PLATFORM=hwcomposer`,
    fuldskærm) — samme biblioteker en x11-platform skal servicere.
A3. **EGL-platform-API-kontrakten** (de-risker opgave A i §2 direkte):
    `vendor_root/usr/local/include/hybris/eglplatformcommon/eglplatformcommon.h`
    definerer præcis, hvilke funktioner en ny platform skal implementere — suppleret
    med `nm -D` på `eglplatform_hwcomposer.so` (samme funktionssæt = facitlisten for
    `eglplatform_x11`). A bliver fra "et par hundrede linjer" til en konkret
    tjekliste.
    **Målt (24. aug 2026):** hele kontrakten ligger i
    `.../eglplatformcommon/ws.h`: `struct ws_module` med `init_module`,
    `GetDisplay`, `Terminate`, `CreateWindow`, `DestroyWindow`,
    `eglGetProcAddress`, `passthroughImageKHR`, `eglQueryString`, `prepareSwap`,
    `finishSwap`, `setSwapInterval`. Platform-`.so`'en skal eksportere de tilsvarende
    `*ws_*`-funktioner + et `ws_module_info`-symbol (facit fra `nm -D` på
    `eglplatform_hwcomposer.so`/`eglplatform_fbdev.so`). `eglplatform_fbdev.so` er
    den nærmeste analog: `FbDevNativeWindow` implementerer ANativeWindow
    (dequeue/queue/lock/cancelBuffer + setSwapInterval) — en x11-platform bliver
    samme form, med XPutImage i queueBuffer.
A4. **Hybris-EGL'ens overflade.** `objdump -T` + `strings` på vendors `libEGL.so`:
    hvilke EGL-entry-points og client-extensioner den reklamerer med
    (`EGL_EXT_platform_base`, `EGL_KHR_surfaceless_context` …). Det er præcis den
    liste, en browser spørger om ved init.
    **Målt (24. aug 2026):** hybris' `libEGL.so.1.0.0` eksporterer komplet
    EGL-API (GetDisplay/Initialize/ChooseConfig/CreateWindow- + Pixmap- +
    PbufferSurface/CreateContext/MakeCurrent/SwapBuffers/QueryString/
    GetProcAddress) + `CreateImageKHR`/`DestroyImageKHR` +
    `_my_eglSwapBuffersWithDamageEXT`. Android-loaderens `libEGL.so` bærer
    extension-strenge: `EGL_KHR_create_context` + `EGL_EXT_create_context_robustness`,
    `EGL_KHR_fence_sync`/`reusable_sync`/`wait_sync`, `EGL_KHR_image(_base)`,
    `EGL_ANDROID_image_native_buffer`, `EGL_ANDROID_presentation_time` m.fl. —
    præcis det sæt en browser spørger om ved init (create-context+robustness,
    fence-sync, image).
A5. **PVR-blob'ens statiske capability-profil.** `strings` på
    `libGLESv2`/`libEGL_POWERVR_ROGUE.so` (`dualos_blobs/system_lib`,
    `dualos_blobs/vendor_lib`) — GL-extensionerne står næsten altid som tekst i
    binæren. Første statiske liste at holde op mod WebRenders krav (risiko C i §2)
    uden at røre boksen.
    **Målt (24. aug 2026)** på `dualos_blobs/vendor_lib/lib/egl/libGLESv2_POWERVR_ROGUE.so`:
    `GL_OES_surfaceless_context`, `GL_EXT_robustness`, `GL_KHR_debug`,
    `GL_OES_texture_float`/`half_float` + `GL_EXT_color_buffer_float`,
    `GL_EXT_draw_buffers`, `GL_EXT_texture_rg`, `GL_KHR_blend_equation_advanced
    (+coherent)`, `GL_KHR_texture_compression_astc_ldr`,
    `GL_IMG_texture_compression_pvrtc(2)`, `GL_EXT_multisampled_render_to_texture`,
    `GL_OES_EGL_image(_external)`, `GL_OES_EGL_sync`, `GL_OES_shader_image_atomic`,
    `GL_OES_standard_derivatives`, `GL_OES_vertex_array_object` m.fl. ES 3.1-core
    dækker UBO/instancing/SSBO (rendereren melder 3.1 på boksen). → Risiko C
    reduceret: PVR reklamerer statisk med de fleste WebRender/WebGL-byggesten;
    resten er runtime-adfærd (buglisten i A1), ikke tilstedeværelse.
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
    **BESLUTNING (24. aug 2026): droppet før prototypen.** Vurdering: M2b
    (eksperiment 3) har allerede bevist præsentationsmekanismen (offscreen →
    XPutImage, 10 fps @ 640x360), A3 giver hele ws_module-kontrakten, og A5
    reducerer C-risikoen. B8 ville kun sætte en perf-forventning og evt. vælge
    XShm over XPutImage — det afgøres billigere i selve platformen. Køres evt.
    senere som tuning.
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
   **AFPRØVET (24. aug 2026) med `eglplatform_x11`:** Firefox ESR 140's
   GL-probe (`glxtest`) loader vores libEGL + hele Android-EGL-kæden
   (`/system/lib/libEGL.so` + `libEGL_POWERVR_ROGUE.so` — bekræftet med strace
   og logd), men `eglGetDisplay` fejler i Firefox' proces med
   **EGL_BAD_DISPLAY** (logd: "eglGetDisplay:218 error 300c") → proben melder
   "libEGL no display" og Firefox falder tilbage til Mesa-software (llvmpipe).
   De samme kald virker i vores klon (`devuan/gpu/eglplatform_x11/`
   `dlopen_egl_test.cpp`/`egl_display_probe.cpp`). Vi har tilføjet
   `EGL_EXT_platform_base` til client-extensionerne (platformens
   `eglQueryString`-hook) og en `eglGetPlatformDisplayEXT`/`eglGetDisplay`-shim
   (`egl_platform_shim.c`), men glxtest når ikke dertil — symbolopslaget rammer
   Android-loaderens version. ÅBEN: hvorfor Android-loaderens `eglGetDisplay`
   fejler i glxtest (display-argument vs. miljø), og hvordan Firefox tvinges til
   hybris-wrapperens version.
   **Næste skridt (Firefox, efter 24. aug 2026):**
   1. Afgør om glxtest kalder `eglGetDisplay` med X-`Display*` (gdb på
      symbolet i glxtest) — Android-loaderen afviser non-default med
      EGL_BAD_DISPLAY (300C).
   2. Tving kaldet gennem hybris-wrapperens `eglGetDisplay`
      (shim-præcedens med `RTLD_GLOBAL` / wrapper-patch).
   3. Kør firefox-esr helt og verificér WebGL: about:support + platformens
      præsent-log (`x11ws: vindue pakket ind` / `present`) + fbdump midt i
      en WebGL-side.
   Værktøjer og beviser: `DOKUMENTATION.md` §5.15c + `devuan/gpu/eglplatform_x11/`.
3. **Prototype af A:** vis et PVR-renderet billede i et X-vindue, mens X kører.
   Virker det, er den store tekniske risiko afklaret — og stykket kan bruges af alle.
   **GJORT (24. aug 2026)** via M2b-vindue-demoen (`window_demo.py` + daemonens
   `frame`-kommando, `devuan/gpu/GLES-DAEMON-PLAN.md` M2b): GLES 3.1/PVR-billede
   renderet af daemonen (offscreen) vist i et Python-oprettet X-vindue via
   XPutImage, mens X kørte — verificeret helt ned i `/dev/fb0` (IsViewable +
   fbdump), 10 fps @ 640x360. Præsentationstesen i §2.A (offscreen →
   XPutImage) er dermed bekræftet som teknisk vej; det manglende stykke er
   selve `eglplatform_x11`-platformen (A).
   **OPFØLGNING (24. aug 2026):** selve `eglplatform_x11`-platformen er nu
   bygget og verificeret — `devuan/gpu/eglplatform_x11/` (GLES 3.1 → gralloc →
   XPutImage → fb0, ~9 fps @ 640x360, cos-scene). To målte fælder løst:
   hybris' EGL-init skifter aktiv VT væk fra X (fix: platformen chvt'er tilbage
   ved første present), og tegning skal gå gennem vinduets egen X-forbindelse
   (fix: klientens Display* som EGL-native-display). Detaljer:
   `GLES-DAEMON-PLAN.md` M4a.
4. **Kodi fra Debian-armhf** + broerne i `/opt/hybris`: producenten kørte netop
   denne kombination i 2016. Den korteste vej til en GPU-accelereret fuldskærms-app,
   og samtidig beviset for at stakken kan bære en rigtig app.

## 5. Foreslået rækkefølge

1. Byg `eglplatform_x11` (A) som selvstændigt projekt — det er forudsætningen,
   uanset hvilken vej man ender på, og det gavner alle brugere af stakken.
2. ~~Kør eksperiment 3 (prototype i X-vindue)~~ — GJORT 24. aug 2026 via
   M2b-vindue-demoen (måling i stedet for gæt).
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
