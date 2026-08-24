# Firefox/WebGL på boks 1 — session-notat 24. aug 2026

Handover fra arbejdssessionen "få WebGL til at virke i firefox-esr på boks 1
(192.168.0.188)" via hybris/`eglplatform_x11`-stakken. Skrevet for at bevare
guldkornene efter kontekst-komprimering; byg videre herfra i en ny session.

Relaterede dokumenter: `BROWSER-VEJE.md` (især §2.A og §4 eksperiment 2),
`devuan/gpu/GLES-DAEMON-PLAN.md` (især M4a), `DOKUMENTATION.md` §5.15c,
`HAANDBOG.md` fælde 19–23.

## 1. Status i ét blik

- **glxtest-proben er GRØN:** `/usr/lib/firefox-esr/glxtest` melder nu
  `TEST_TYPE=EGL`, `VENDOR=Imagination Technologies`,
  `RENDERER=PowerVR Rogue G6110`, `VERSION=OpenGL ES 3.1 build 1.4@3632227`.
  Ingen Mesa/GLX-fallback mere.
- **Hele firefox-esr fejler stadig i WebRender:** hardware-GL-kontekst kan ikke
  oprettes → "Fallback WR to SW-WR" (software WebRender/llvmpipe). To målbare
  fejlmønstre (se §7).
- Planens skridt 1–4 er gennemført (find boks, verificér stak, spor glxtest med
  gdb, tving kald gennem wrapperen). Skridt 5 (hel Firefox + about:support +
  fbdump) er i gang, men blokeret på kontekst-oprettelsen.

## 2. Det vigtigste guldkorn — hvorfor glxtest fejlede (LØST)

**glxtest henter kerne-EGL-funktioner gennem `eglGetProcAddress`, IKKE dlsym.**
I `toolkit/xre/glxtest/glxtest.cpp` (`get_egl_status`):

```cpp
eglGetProcAddress = dlsym(libegl, "eglGetProcAddress");   // wrapperens
eglGetDisplay    = eglGetProcAddress("eglGetDisplay");    // → Android-intern!
```

Wrapperens `eglGetProcAddress` (`/opt/hybris/libEGL.so.1` @+0x47f8) har denne
kæde: special-case-strcmp → dlsym(platform-handle) → `ws_eglGetProcAddress`
(platformens egen) → **Android-loaderens `eglGetProcAddress`**.

Vores platform returnerede NULL for kerne-EGL-navne (delegere til
`eglplatformcommon_eglGetProcAddress`) → Android-loaderens **interne**
`eglGetDisplay` (+0x50c0 i `/system/lib/libEGL.so`) vandt → den afviser
non-NULL-displays med logd "eglGetDisplay:218 error 300c" (kun
`EGL_DEFAULT_DISPLAY`=0 accepteres) → glxtest meldte "libEGL no display".

Målt på boksen (probe `egl_getproc_probe2`):

```
dlsym(libegl, eglGetProcAddress)=0xf75147f9        <- wrapper (+0x47f9)
eglGetProcAddress("eglGetDisplay")=0xf7512d9d      <- efter fix: wrapper (+0x2d9d)
  (før fix: 0xf6bc50c1 = Android-intern; NULL ved non-NULL input, 300C)
eglGetProcAddress("eglInitialize")=0xf7512e3d      <- wrapper
eglGetProcAddress("glGetString")=0xf6fcdaf1        <- Android GLESv2 (ok)
```

Simpel mimictest med dlsym var et vildspor: dlsym fra handle gav altid
wrapperens version. **Man skal teste gennem `eglGetProcAddress`, præcis som
glxtest gør.**

## 3. Ændringer lavet i repoet (git status: kun `eglplatform_x11.cpp`)

### 3a. `devuan/gpu/eglplatform_x11/eglplatform_x11.cpp` (arbejdsrepo)

Tilføjet 48 linjer:

1. `#include <dlfcn.h>`.
2. `x11ws_wrapper_symbol(name)`: dlopen("/opt/hybris/libEGL.so.1",
   `RTLD_NOW|RTLD_NOLOAD`, ellers `RTLD_NOW`) + `dlsym(handle, name)` —
   returnerer wrapperens EGEN eksport.
3. Stubs `x11ws_eglQueryDeviceStringEXT` (→ NULL) og
   `x11ws_eglQueryDisplayAttribEXT` (→ EGL_FALSE) — glxtest kræver dem
   non-NULL (check i `get_egl_gl_status`) og kalder queryDisplayAttrib direkte.
4. `x11ws_eglGetProcAddress`: for ethvert navn undtagen `"eglGetProcAddress"`
   (rekursionsfare) forsøg wrapper-symbol først; ellers fald tilbage til
   `eglplatformcommon_eglGetProcAddress`. Effekt: kerne-EGL-kald gennem
   `eglGetProcAddress` bliver i hybris/X11-stien i stedet for Android-intern.

### 3b. Boksen: glxtest-binær patchet (dybde-tjek)

`/usr/lib/firefox-esr/glxtest` @ fil-offset 0x2777: byte `0x18` → `0x10`.
Ændrer ARM-instruktionen `e3510018` (`cmp r1, #24`) → `e3510010`
(`cmp r1, #16`) i `x11_egltest` (Bug 1667621: "DefaultDepth() is %d, expected
to be 24"). Boksens X kører 16-bit (fb0=16 bpp), så tjekket krævede 24 og
smid EGL-resultatet væk → GLX/Mesa-fallback.

- Backup: `/root/glxtest.orig`
- md5 før: `1baa32f007ad4b8a6cbd95b0e4f1d4da`; efter: `4b6117e1dcf1cae5a7b30bb071add129`
- `DefaultDepth` er et Xlib-makro (læser `dpy->depths[screen].depth`) — der er
  INGEN `XDefaultDepth`-funktion/symbol at break'e på i gdb.

## 4. Boksens byg/deploy-procedure for platformen

```sh
scp devuan/gpu/eglplatform_x11/eglplatform_x11.cpp root@192.168.0.188:/root/eglplatform_x11/
ssh root@192.168.0.188 'bash /root/eglplatform_x11/build_box.sh'
```

`build_box.sh` bygger i `/root/eglplatform_x11/out/` og kopierer til
`/usr/local/lib/libhybris/eglplatform_x11.so` (det er den fil libEGL loader
for `EGL_PLATFORM=x11`). Deployet md5:
`11527965e5bfeac2d7c1caf4191e69ff` (verificeret = out/).

## 5. Verificeret stak (skridt 2) — kommando og output

```sh
LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
EGL_PLATFORM=x11 DISPLAY=:0 /root/egl_display_probe
```

```
CLIENT-EXTENSIONS: EGL_KHR_get_all_proc_addresses EGL_EXT_platform_base
  EGL_EXT_platform_x11 EGL_KHR_platform_x11
NULL (EGL_DEFAULT_DISPLAY): eglGetDisplay=0x1, eglInitialize=1 (1.4)
Display*: eglGetDisplay=0x1, eglInitialize=1 (1.4)
```

VT returnerer til tty7 og HDMI=1 efter kørsel (exit-dansen er harmløs med
system_shim).

## 6. Viden om glxtest (kilde + målt)

- Kilde (master): `toolkit/xre/glxtest/glxtest.cpp` — hentet lokalt til
  `/tmp/glxtest_gh.cpp`. ESR 140-binæren matcher strukturen (strenge:
  "childgltest", "x11_egltest", "get_egl_status", "libEGL no display",
  "eglGetDisplayDriverName").
- `main()` kalder `childgltest()` direkte — **ingen fork** i ESR 140 (den gamle
  kommentar i toppen er forældet). "childgltest" er bare en funktion.
- `x11_egltest`: XOpenDisplay → `get_egl_status(dpy)` → dybde-tjek (patchet,
  §3b) → xrandr → `record_value("TEST_TYPE\nEGL\n")`.
- `get_egl_status` via eglGetProcAddress: eglGetDisplay, eglInitialize,
  eglTerminate, eglGetDisplayDriverName (valgfri).
- `get_egl_gl_status` kræver non-NULL: eglChooseConfig, eglCreateContext,
  eglDestroyContext, eglMakeCurrent, **eglQueryDeviceStringEXT**. Kalder
  direkte: eglBindAPI, eglQueryDisplayAttribEXT (NULL → crash!). Bruger
  `{EGL_CONTEXT_MAJOR_VERSION, 3}` med ES2-fallback og dlsym-fallback for
  glGetString.
- Patchede glxtest-kørsler (før Firefox): "libEGL no display" → (efter §3a)
  "libEGL missing methods for GL test" → (efter stubs) PowerVR/EGL grøn.

## 7. Hele Firefox — de to fejlmønstre (nuværende blokering)

Kørsel:

```sh
cd /root && LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so" \
LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 MOZ_X11_EGL=1 \
MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test.html
```

Platformen initialiserer (`x11ws: init_module færdig`, `x11ws: GetDisplay
modtog klientens X-forbindelse`), men WebRender-hardwarekontekst fejler:
"Fallback WR to SW-WR". Målt med gdb (log: `/root/ff_gdb2.log`,
`ff_gdb3.log`, `ff_gdb4.log`):

- **Mønster A (hardware-WR/GLES, 0x300c):** `eglBindAPI(0x30a2=ES)` lykkes,
  derefter fejler `eglCreateContext` med EGL_BAD_DISPLAY (0x300c) **uden at
  ramme wrapperens eglCreateContext** (0 hits på breakpoint). Dvs. kaldet går
  gennem en anden funktionspointer (Android-intern via en ikke-kortlagt vej —
  libepoxy mistænkt, se §9).
- **Mønster B (desktop-GL, 0x3000):** `eglBindAPI(0x30a0=GL)` → wrapperens
  `eglCreateContext` rammes 2× (robustness-attributter: 0x3098 MAJOR 3,
  0x31bd/0x31bf EXT reset-strategy, 0x30fc KHR flags robust-access) → kontekst
  oprettes, `eglMakeCurrent(dpy, pbuffer, pbuffer, ctx)` **lykkes**, derefter
  `eglMakeCurrent(nil,nil,nil)` (oprydning) → fejl 0x3000 (EGL_SUCCESS =
  forældet error; Init fejler efter MakeCurrent — `GLContextEGL::Init` →
  `GLContext::Init` → `InitImpl`).
- Wrapperens `eglCreateWindowSurface` blev **aldrig** ramt (0 hits) i nogen
  batch, og "x11ws: vindue ... pakket ind" kom aldrig → overfladen er en
  pbuffer (CreateFallbackSurface), dvs. Firefox starter offscreen/headless.
- gdb backtrace på `eglGetError` (der printer fejlen) er ens og strippet
  (libxul), så ingen symbol-info.
- Android-loaderens INTERNE `eglCreateContext(+0x6534)` VIRKER med display 0x1
  + config fra wrapperens chooseConfig (målt direkte via `android_dlopen`/
  `android_dlsym` i proben `android_internal_probe`) — så 0x300c-kaldet må
  sende et andet display/config, eller komme fra libepoxy med anden
  opslagskæde.

## 8. Wrapperen — adresser og struktur (målt)

`/opt/hybris/libEGL.so.1` (nm -D, alle `T egl*`): eglGetDisplay @0x2d9c,
eglGetError @0x2c90, eglInitialize @0x2e3c, eglChooseConfig @0x3008,
eglCreateWindowSurface @0x30f4, eglBindAPI @0x36c8, eglCreateContext @0x3e44,
eglMakeCurrent @0x3f5c, eglGetProcAddress @0x47f8, eglTerminate @0x2eac.

`eglGetProcAddress`-kæden (disassembly): 4 special-case-strcmp (tabellens
navne/adresser ikke afkodet) → tilstandsmaskine (1/2/3) med glibc `dlopen` af
`EGL_PLATFORM`-lib + `dlsym(handle, navn)` → `ws_eglGetProcAddress` (platform)
→ Android (`android_eglGetProcAddress`). wrapperens `eglGetDisplay` kalder
altid Android-intern `eglGetDisplay(0)` + `ws_GetDisplay(original_arg)`.

Android-loaderen: `/system/lib/libEGL.so` (boks-md5 `24769cda…`;
`dualos_blobs/system_lib/libEGL.so` md5 `b14ee7c0…` — identisk indhold).
Intern `eglGetDisplay` @+0x50c0: kun r0==0 accepteres, ellers log linje 218
error 300C. Intern `eglCreateContext` @+0x6534.

## 9. Åbne spørgsmål / næste skridt (prioriteret)

1. **Byg og kør epoxy-mimic'en** (kilde `/tmp/epoxy_mimic.c` på laptoppen;
   kørslen blev afbrudt FØR scp, så intet ligger på boksen endnu). Den tester
   libepoxy's EGL-dispatch (`eglCreateContext`/`eglGetDisplay` eksporteret af
   libepoxy.so.0), som Firefox/libxul linker mod. Hvis epoxy's create fejler
   0x300c, er rodårsagen til mønster A fundet. Byg på boksen med `-lEGL
   -lhybris-common -ldl -lrt -lm` + `-I/usr/local/include`.
2. **Find ud af hvilken eglCreateContext-pointer mønster A bruger.** Mistanke:
   libepoxy løser via dlsym(RTLD_DEFAULT) eller eglGetProcAddress og ender i
   Android-intern med et ugyldigt display. Kør gdb med break på wrapperens
   `eglGetProcAddress` (navne-log) og evt. på Android-intern create (adresse =
   libEGL-base +0x6534, base fra `/proc/<pid>/maps`).
3. **Mønster B (0x3000):** kontekst+MakeCurrent virker, Init fejler. Prøv
   `MOZ_LOG="GLContext:5"` for at se hvor Init dør (glGetString? anden
   MakeCurrent?). Kig på `GLContext::InitImpl` (kilde i `/tmp/GLContext_esr140.cpp`).
4. Når kontekst virker: verificér WebGL med about:support (fx
   `firefox --screenshot`, eller læs `about:support` via CDP), platformens
   præsent-log ("x11ws: vindue ... pakket ind" + present) og fbdump
   (`dd if=/dev/fb0 of=/root/ff_fb.raw bs=3840 count=1080`).
5. Opdater `DOKUMENTATION.md` §5.15c og `BROWSER-VEJE.md` §4 med resultaterne
   (inkl. dette notat som reference).

## 10. Filer og logs (boksen /root/)

- Probe/verifikation: `egl_display_probe`, `dlopen_egl_test`,
  `egl_getproc_probe`, `egl_getproc_probe2`, `ff_egl_mimic` (+`ff_mimic.txt`),
  `android_internal_probe` (+`android_internal.txt`), `epoxy_mimic`
  (+`epoxy_mimic.txt`, ufuldstændig), `glxtest_mimic`, `glxtest_mimic.c`.
- Patchet glxtest: `/usr/lib/firefox-esr/glxtest` + backup `/root/glxtest.orig`.
- gdb-spor: `ff_gdb.log`, `ff_gdb2.log`, `ff_gdb3.log`, `ff_gdb4.log`
  (+ `_clean.log` uden NUL), `glxtest_gdb2.log`, `glxtest_gdb.cmd` (obs:
  indeholder `finish`-version — ikke brug; `finish` i kommandoblokke
  deadlocker gdb ved dlopen/dlsym-breakpoints).
- Firefox: `ff_run.log`, `ffprof/` (profil med user.js), `webgl_test.html`,
  `ff_user.js`. Også: `glxtest_out.txt`, `glxtest_new.txt`, `glxtest_strace.log`,
  `mimic_out.txt`, `probe2_out.txt`.
- Kildefiler hentet til laptoppen i `/tmp/`: `glxtest_gh.cpp`,
  `GLContextProviderEGL.cpp`, `GLContextProviderEGL_esr140.cpp`,
  `GLLibraryEGL.cpp`, `GLLibraryEGL.h`, `GLContextEGL_esr140.h`,
  `GLContext_esr140.cpp`.
- Kilder (URL'er): raw.githubusercontent.com/mozilla/gecko-dev master/release +
  hg.mozilla.org/releases/mozilla-esr140/raw-file/tip/... (searchfox er 406,
  raw.githubusercontent virker med curl).

## 11. Miljø, kommandoer og fælder (gentaget for en ny session)

- Boks 1: `ssh -i /home/kristian/.ssh/geekbox_key root@192.168.0.188`
  (sandbox fejler af og til med "socket: Operation not permitted" — gentag/eskalér).
- Miljø: `LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so"`
  `LD_LIBRARY_PATH=/opt/hybris` `EGL_PLATFORM=x11` `DISPLAY=:0`.
  Til Firefox desuden `MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1
  MOZ_DISABLE_GPU_SANDBOX=1`.
- `system_shim.so` er nødvendig mod glibc-2.41-EFAULT i hybris-processer;
  `egl_platform_shim.so` eksporterer eglGetPlatformDisplayEXT/
  eglGetPlatformDisplay/eglGetDisplay (kilden i
  `devuan/gpu/eglplatform_x11/egl_platform_shim.c`).
- Oprydning: `pkill -9 -x firefox-esr` — **aldrig** `pkill -f firefox` (dræber
  SSH-skallet, da kommandolinjen indeholder "firefox"). Tjek bagefter
  `cat /sys/class/tty/tty0/active` (skal være tty7) og
  `cat /sys/class/display/HDMI/enable` (skal være 1); `chvt 7` ved behov.
- Hybris-EGL-init skifter aktiv VT (fælde 19); tegning skal gå gennem vinduets
  egen X-forbindelse (fælde 20); popen/pgrep fejler i hybris-processer
  (fælde 21); `/proc/cmdline` har NUL-argumenter (fælde 22).
- Boksens gdb: brug breakpoint-kommandoer med kun printf + `continue`; undgå
  `finish` i kommandoblokke. Android-loaderens libs (bionic) er usynlige for
  gdb — bryd i stedet på wrapperens funktioner eller på absolut adresse fra
  `/proc/<pid>/maps`.
- `rg` findes ikke på boksen — brug `grep`.

## 12. Uafklarede detaljer / ting at være opmærksom på

- "BROWSER-VEJE.md" ligger i **repoets rod** (ikke `devuan/gpu/`).
- `dlsym_trace.so` på boksen er i stykker (interposer returnerer NULL) — brug
  den ikke.
- Firefox loadede wrapperens `eglGetDisplay` med X-`Display*` (ikke
  EGL_DEFAULT_DISPLAY) i gdb-sporet — fra libxul, ikke fra shim'en
  (`[egl-shim]`-linjer optrådte ikke). Shim'en blev altså ikke kaldt i
  Firefox-stien; wrapperens egen funktion blev brugt direkte.
- `logcat -d` på boksen gav ingen egl-linjer (tom/ikke fungerende) — Android-
  loaderens fejl-log er ikke tilgængelig ad den vej i praksis.
- Firefox' profil indeholder `gfx.x11-egl.force-enabled=true`; testside
  `/root/webgl_test.html` (webgl2/webgl canvas med animation).
