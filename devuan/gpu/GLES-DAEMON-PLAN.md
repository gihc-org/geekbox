# GLES-daemon + Python-frontend — arbejdsplan (aug 2026)

Plan for TODO-punktet "Python-frontend + GLES-daemon" (TODO.md): Python tegner UI
direkte på `/dev/fb0`, taler med en C-daemon over en unix-socket (JSON-linjer), og
daemonen renderer GLES offscreen og blitter resultatet til fb0. X stoppes under
brug; strøm-cyklus bagefter (reglerne i DOK §5.15). Arkitektur-kortet:
`devuan/gpu/README.md`.

**Hovedformål (præciseret af brugeren 24. aug 2026):** teste
GLES/hybris-stakken via et vindue oprettet af Python (M2b-vejen:
`window_demo.py` + daemonens `frame`-kommando, X kører). fb0-kiosken (M2/M3)
er en sekundær alternativ visningsvej — nyttig som kiosk-UI, men ikke
hovedmålet.

## Status (23. aug 2026)

- [x] Beslutning: **cross-bygning på laptoppen** (ikke på boksen)
- [x] Værktøj installeret: `arm-linux-gnueabihf-g++` 13.3 (Mint/Ubuntu noble-pakke)
- [x] Byggemateriale verificeret i repoet: headers i `vendor_root/usr/local/include`,
      ARM-libs i `vendor_root/usr/local/lib` (libEGL, libGLESv2, libhybris-common,
      libhybris-hwcomposerwindow, libhybris-eglplatformcommon, libandroid-properties,
      libhardware, libsync — alle ELF32 ARM EABI5, tjekket med `file`)
- [x] M0: baseline-byg + verifikation på boks 1 (23. aug 2026)
- [x] M1: `gles_daemon.c` (23. aug 2026)
- [x] M2: `frontend.py` (23. aug 2026)
- [x] M2b: `frame`-kommando + `window_demo.py` — X-vindue-demo (24. aug 2026;
      implementeret + 10 fps målt + skærm-verifikation færdig — se M2b)
- [x] M3: testcyklus på boksen (24. aug 2026 — se M3; side-spor, se hovedformål)
- [ ] M4: integration i `gpu_setup.sh` + dokumentation

## Faste beslutninger (med begrundelse)

1. **Cross-byg på laptoppen.** `g++-arm-linux-gnueabihf` + headers/libs fra
   `vendor_root`. Boks 1 er i dag det eneste byggehost (`gpu_setup.sh` henter
   færdige binærer derfra); cross-bygning gør byggekæden reproducerbar og
   uafhængig af en bestemt boks, giver hurtig iteration uden ssh-ture og
   belaster ikke boksen (brownout-historien, DOK §5.14). Boksens eget gcc
   beholdes som fallback. Verifikation foregår stadig på boks 1 (M0/M3).
2. **Offscreen via FBO, ikke pbuffer.** hwcomposer-platformens
   pbuffer-understøttelse er ikke verificeret; FBO er core i GLES 3.1 (rendereren
   melder 3.1 på boksen). Vinduesfladen fra `test_triangle` bruges kun som
   EGL-"current"-holder — der kaldes **aldrig** `eglSwapBuffers`, så
   hwc-præsentationen kører ikke. Render → `glReadPixels` fra FBO →
   CPU-konvertering → blit til fb0.
3. **1:1-blit i v1** (ingen scaling): frontenden vælger en rect; daemonen
   renderer scenen i samme størrelse og kopierer pixel-for-pixel fra FBO til
   fb0-recten. Scaling er en senere udvidelse.
4. **fb0-formatet læses ved kørsel** (FBIOGET_VSCREENINFO: bits_per_pixel +
   farve-offsets) — EDID-racen kan give 16/24/32 bpp; myinit normaliserer, men
   daemonen skal ikke antage et format.
5. **Protokol v1: JSON-linjer, synkron.** Frontenden sender én kommando pr. linje
   og venter på svaret; daemonen renderer + blitter før svaret. Frontenden tegner
   sin UI (baggrund/rammer/tekst), beder daemonen blit sin rect og tegner evt.
   tekst ovenpå bagefter — racer om fb0 undgås i v1 ved denne rækkefølge og
   adskilte rects.

## Milestones

### M0 — baseline: test_triangle bygger cross (23. aug 2026)

Verificeret link-linje (cross, g++ 13.3 → kører på boks 1, gcc 14.2):

    arm-linux-gnueabihf-g++ -O2 -o test_triangle test_triangle.cpp \
      -I vendor_root/usr/local/include \
      -I vendor_root/usr/local/include/android                  # <hardware/...>
      -I vendor_root/usr/local/include/hybris/eglplatformcommon # "nativewindowbase.h"
      -L vendor_root/usr/local/lib -Wl,-rpath-link,<samme sti> \
      -lhybris-hwcomposerwindow -lEGL -lGLESv2 -lhardware -lm

    arm-linux-gnueabihf-gcc -shared -fPIC -O2 -o system_shim.so system_shim.c

Bygningen er pakket ind i `devuan/gpu/build.sh` — reproducerbar med én kommando.

- [x] Link-linjen fastlagt. To header-fælder: `hwcomposer_window.h` bruger
      Android-konventionen `<hardware/...>` (kræver `-I .../include/android`), og
      inkluderer `nativewindowbase.h` med bare navn (kræver
      `-I .../include/hybris/eglplatformcommon`).
- [x] Cross-byg `test_triangle` + `system_shim.so` fra repoet.
- [x] Verificeret på boks 1 (192.168.0.188): `GL_VERSION=OpenGL ES 3.1
      build 1.4@3632227`, `GL_RENDERER=PowerVR Rogue G6110`, 500 frames —
      identisk med originalen. `readelf -d`-NEEDED-listen matcher 1:1. Cross-byggede
      `system_shim.so` fanger `system()`-kaldene uden EFAULT. De to
      "Library 'libRL.so'/'libPVRDebugger.so' not found"-advarsler er de sædvanlige,
      harmløse bionic-linker-advarsler om valgfrie debug-libs.
- [x] `file`-tjek: ELF32 ARM EABI5, interpreter `/lib/ld-linux-armhf.so.3`.

Resultat: boksens `/root/test_triangle_cross` + ny `/root/system_shim.so` (cross).
NB: den gamle `system_shim.so` blev overskrevet med den cross-byggede (samme kilde;
adfærd verificeret i kørslen). Boksen skal strøm-cykles efter sessionen (regel 2).

### M1 — `devuan/gpu/gles_daemon.c` (23. aug 2026)

- [x] `test_triangle.cpp`'s init genbrugt (hwc-modul, vindue, EGL, GLES2-shader).
- [x] Unix-socket-lytter `/tmp/gles.sock` (stale socket fjernes ved start;
      SIGTERM/SIGINT rydder op).
- [x] Minimal dependency-fri JSON-subset-parser.
- [x] Kommandoer: `ping`, `fb`, `scenes`, `render`, `clear`, `quit` — alle testet
      på boks 1 med `socktest.py`.
- [x] Scene `triangle` = cos-mønster-shaderen; FBO 1920x1080 + `glReadPixels` →
      konvertering til fb0-format → mmap-blit (16/24/32 bpp via `fb_var`).
- [x] Verificeret på boks 1: fb0-dump (mmap, 4.147.200 bytes RGB565) viser scenen
      i rect'en; render ~87-123 ms pr. 960x540-frame (optimeres senere, fx i M4).

**Vigtig opdagelse — `glReadPixels` var død i wrapperen:** hybris' libGLESv2.so.2
har en tom `_glReadPixels`-slot (BSS-offset 0x101dc) — init'ens `android_dlsym`
løste den ikke. Kald → SIGSEGV (NULL-pointer) → machybrisegl's signal-fælde
(`catch_exit_signals`) fanger det → `refresh_display`-dansen (chvt + display-toggle)
→ `exit(42)`. Fundet med strace (SIGSEGV `si_addr=NULL`) + gdb
(`glReadPixels_wrapper` kaldte adresse 0x0) + disassembly. Fix:
`patch_readpixels()` i gles_daemon.c — resolve `glReadPixels` via
`hybris_dlsym(hybris_dlopen("libGLESv2.so"), ...)` fra DDK'en
(`/system/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so` eksporterer den) og skriv
pointeren ind i slottet. Verificeret: FBO- OG default-framebuffer-readback virker
nu (`devuan/gpu/readback_probe.cpp`).

**Andre målte fælder:**
- `/dev/fb0`'s read() giver kun 2.073.600 bytes (1920x1080x1) — brug mmap til
  dump, ikke dd/read.
- `EGL_PLATFORM=null` crasher ved `eglCreateWindowSurface(NULL)` (NULL-deref i
  `android_createDisplaySurface`-vejen → exit(42)) — null-platformen er IKKE en
  genvej på denne boks; hwcomposer-platformen + patchen er vejen (B7-proben fra
  BROWSER-VEJE.md er dermed besvaret med nej).
- Daemonen kalder aldrig `eglSwapBuffers` → ingen hwc-præsentation → `service
  nodm start` virkede bagefter. Om HDMI viser billedet uden strøm-cyklus skal
  bekræftes på TV'et; regel 2 står til den er målt afkræftet.

### M2 — `devuan/gpu/frontend.py` (23. aug 2026)

- [x] Python 3, kun stdlib. Åbn `/dev/fb0` O_RDWR + mmap; var/fix-info læst via
      ioctl (arvet teknik fra `fb_overscan.py`).
- [x] Tegn: fyld, rammer, tekst via indlejret 5x7-bitmapfont (A-Z, 0-9,
      tegnsætning + æ/ø/å; Æ/Ø/Å normaliseres til AE/O/A).
- [x] Socket-klient: JSON-linjer ind, svar ud (synkront).
- [x] Demo-loop: baggrund + ramme + titel/statuslinjer, animeret fase-sweep af
      `triangle`-scenen i N frames. Afvigelse fra planen: der køres ingen `clear`
      til sidst — sidste frame efterlades på skærmen (statuslinjen siger det),
      så resultatet kan ses/dumpes.
- [x] Flag: `--fb`, `--socket`, `--rect WxH+X+Y`, `--frames`, `--fps`,
      `--no-gles`, `--dump`, `--title`.
- [x] Accept: kører på boksen uden X; fb0 dumpet til PNG viser rammer, tekst og
      GLES-scene (metoden fra DEBUG-SORT-SKAERM) — verificeret på boks 1:
      baggrund (16,20,24) = (18,22,30) i RGB565, ramme (120,188,248), titel 240
      pixels, GLES-rect 102 farver (cos-mønster), statuslinje til stede.
      `--no-gles`: rect er tom baggrund, UI tegnet — verificeret.

**To fælder fundet og løst undervejs (begge målt på boksen):**
- `line_length` i `fb_fix_screeninfo` ligger på byte-offset **44** på 32-bit ARM,
  ikke 42: `__u32` skal 4-byte-alignes efter tre `__u16`-felter, så kompilatoren
  (og kernen) indskyder 2 bytes padding. En "pakket" Python-formatstreng gør det
  ikke — brug eksplicitte offsets + sanity-check (frontend.py har kommentar).
- `--rect`-parsing: `960x540+480+270` skal splittes med `split("+", 1)`, ellers
  bliver der tre dele.

### M2b — `window_demo.py`: X-vindue-demo (aug 2026)

**HOVEDMÅLET** (præciseret af brugeren 24. aug 2026): *"teste
GLES/hybris-stakken via et vindue oprettet af python"* — `window_demo.py`
åbner et X-vindue, snakker med daemonen over socketten og viser GLES-scenen i
vinduet. **X skal IKKE stoppes.** Vejen blev oprindeligt noteret som "et nyt
spor oven på M2", men er per bruger-præcisering hovedformålet;
frontend.py-kiosken (M2/M3) er sekundær.

**Forbindelse til `BROWSER-VEJE.md`:** vindue-demoen er eksperiment 3
("prototype af A — PVR-renderet billede i X-vindue, X kører") og bekræfter A's
præsentationstese (offscreen → XPutImage). Næste skridt på browser-vejen er
selve `eglplatform_x11`-platformen (BROWSER-VEJE §2.A).

- [x] `frame`-kommando i `gles_daemon.c`: renderer scenen i FBO og RETURNERER rå
      pixels (JSON-header-linje + binær) — ingen fb0-blit, så X kan køre.
- [x] `fmt="rgb565"` (2 bytes/px, little-endian R5G6B5): C-pakning i daemonen →
      Python vender kun rækkerne om → **10 fps målt** (60 frames på 6,0 s,
      640x360; daemon-render ~56-70 ms). Uden den var Python-pakningen ~2 s/frame.
- [x] `window_demo.py`: ctypes + libX11 (hverken tkinter eller PIL findes på
      boksen), `XCreateSimpleWindow` + `XPutImage`, WM_DELETE_WINDOW + Escape,
      5x7-tekst genbrugt fra frontend.py.
- [x] Verificeret på boks 1: 12/20/60 frames uden crash; daemonen får `quit` og
      lukker (ingen efterladte); XPutImage ind i et vindue VIRKER på skærmen
      (C-test `diagnostik/test_x8_putimage.c`: blå rect i vindue landede på fb0).
- [x] **SKÆRM-VERIFIKATION FÆRDIG (24. aug 2026):** demo-vinduets indhold når
      `/dev/fb0`. På boks 1 (192.168.0.188) med X kørende: `clearroot` først,
      daemon startet, `window_demo.py --frames 150 --fps 10` (150 frames på
      15,4 s ≈ 9,7 fps). Midt i kørslen: vindue `0x1800001`
      "GLES-daemon demo — PowerVR G6110 (640x360)" var **IsViewable** på
      `+640+357` (openbox-frame `+638+334`), og to `fbdump`-dumps (1920x1080
      RGB565, mmap) viser cos-mønsteret + hvid status-tekst præcis i
      vindue-området (88/91 unikke farver; 5394 hvide label-pixels i bbox
      x646-995 y362-377). De to dumps adskiller sig på 19.456 px, og de varme
      cos-bånd forskydes mellem dumpsene → fase-sweep kører. Dumpene lå som
      `/root/fb_m2b_{1,2}.raw` (slettes ved næste oprydning).
      **OBS (2. kørsel, 24. aug):** ved `quit` kører daemonens exit-dans og kan
      efterlade `/sys/class/display/HDMI/enable=0` — skærmen bliver sort, selvom
      X lever (fælde 8). Repeteret kørsel (600 frames @ 9,9 fps) verificerede
      det samme vindue → fb0-resultat, og X crashede IKKE (kun HDMI-displayet
      var slået fra).
- [x] **FIX — ingen exit-dans (24. aug 2026):** `daemon_exit()` kalder `_exit()`
      efter eksplicit oprydning (socket-unlink + `fflush`), så machybrisegl/
      hybris' atexit-displaydans (chvt + display-toggle, kan slå HDMI fra eller
      hænge processen) aldrig kører. Verificeret på boks 1: efter `quit` ingen
      `[system-shim]`-linjer i loggen, proces væk, aktiv VT og HDMI-enable urørt.

**Rødder fundet undervejs (alle målt på boksen, ikke gæt):**
1. **XCreateImage format = ZPixmap (2), ikke 1.** Python-koden sendte 1
   (XYPixmap) → XPutImage gik i bitmap-vejen → memcpy-crash (SIGSEGV i libc,
   gdb-backtrace; r8=r9=40 = width/8 afslørede plan-logikken). C-tests brugte
   ZPixmap og virkede. Fix: `format=2` i window_demo.py.
2. **openbox (LXDE) flytter nye vinduer.** Demo-vinduet (anmodet om 120,80) lå
   ved (640,370) (xwininfo: frame +638+347, client +640+370). Tidligere "sorte
   aflæsninger" var aflæsning af det forkerte sted på skærmen.
3. **X' fbdev-driver maler ikke root-baggrund ved opstart** — gamle direkte-
   fb0-blits (kiosk-UI, testrektangler) bliver stående på skærmen. Brug
   `diagnostik/clearroot.c` før visuel verifikation.
4. **scp kan ikke overskrive en kørende eksekverbar** (sftp-server:
   "dest open Failure") — dræb daemonen først.
5. **Efterladte daemoner kan hænge i socket-read og ignorere SIGTERM**
   (`skb_recv_datagram`) — `pkill -9` virker.
6. **Daemonens START skifter aktiv VT (målt: til vt10).** hwc-init'en kører en
   VT-dans også ved start. Kør `chvt 8` (X' vt) EFTER daemon-start og FØR
   demoen — ellers viser skærmen ikke X' indhold, og fb0 kan indeholde
   konsol-billedet (målt 24. aug 2026).
7. **Daemonens quit-exit-dans kan efterlade processen kørende.** Efter `quit`
   (chvt 7 → display-toggle → chvt 11 → chvt 7) kan processen stadig ligge i
   `pgrep`; dræb med `pkill -9 -x gles_daemon` og kør `chvt 8` tilbage til X
   (målt 24. aug 2026).
8. **`quit`-exit-dansen kan slå HDMI-displayet FRA.** Dansen kører
   `echo 0 > /sys/class/display/*/enable` (og normalt `echo 1` bagefter); blev
   processen dræbt midt i dansen (eller `echo 1` fejler), står displayet på 0 →
   sort skærm, selvom X kører (målt 24. aug 2026: `HDMI/enable=0`, X i live på
   vt8; fix: `echo 1 > /sys/class/display/HDMI/enable`, ellers strøm-cyklus).
   Undgå hele dansen: kør `window_demo.py --keep-daemon` og dræb bagefter med
   `pkill -9 -x gles_daemon` (SIGKILL kører ingen exit-handlers) + `chvt 8`.
   **LØST (24. aug 2026):** `daemon_exit()` med `_exit()` i gles_daemon.c
   springer over atexit-dansen — `quit` (og SIGTERM/SIGINT-stien) er nu sikkert,
   og hverken aktiv VT eller HDMI-enable ændres ved afslutning (målt).

Diagnostik-værktøjer i `devuan/gpu/diagnostik/` (genbrug i næste session):
- `test_x7_fb_truth.c`: tegner i X og læser /dev/fb0 direkte — afgør om serveren
  overhovedet tegner (JA: XFillRectangle på root lander på fb0).
- `test_x8_putimage.c`: rydder gamle testvinduer, tester XPutImage på root + i
  vindue, dumper fb0.
- `fbdump.c`: mmap-dump af hele fb0 (`read()` giver kun halvdelen — M1-fælde).
- `clearroot.c`: fylder root sort — ren tavle.
- `rgb565_to_png.py`: RGB565-dump → PNG (visuel/ASCII-analyse).

### M3 — testcyklus på boks 1

NB: M3 tester kiosk-vejen (fb0, X stoppet) — et side-spor. Hovedmålet (test af
GLES/hybris via Python-vindue) er M2b, verificeret ovenfor.

```bash
devuan/find_box.sh                          # find IP
scp -i ~/.ssh/geekbox_key gles_daemon frontend.py root@<ip>:/root/
ssh -i ~/.ssh/geekbox_key root@<ip> 'service nodm stop'
ssh -i ~/.ssh/geekbox_key root@<ip> 'sh /root/gpu_up.sh'
ssh -i ~/.ssh/geekbox_key root@<ip> \
  'LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=hwcomposer /root/gles_daemon &'
ssh -i ~/.ssh/geekbox_key root@<ip> 'python3 /root/frontend.py'
# verificér: daemon-log + fb0-dump → PNG
# bagefter: strøm-cykl boksen (DOK §5.15)
```

- [x] Daemon starter, `ping`/`fb` svarer; frontend tegner UI på fb0 (24. aug
      2026, boks 1): `service nodm stop` → `gpu_up.sh` → daemon → `frontend.py
      --frames 30 --fps 5 --dump /root/fb_m3.raw`; daemon-log viser 30 renders,
      frontend svarer "færdig" og dumper 4.147.200 bytes.
- [x] `triangle`-scenen vises korrekt i rect'en (fb0-dump verificeret): BG
      (18,22,30)→0x10A3 dominerer, ramme (120,190,255)→0x7DFF, titel/status-
      tekst 0xEF9E, GLES-rect 102 unikke farver (cos-mønster, varmt punkt
      ~0xACCE i midten) — matcher M2-signaturen.
- [x] X kan startes igen: `service nodm start` → Xorg :0 på **vt7** (aktiv VT 7
      matcher), lxpanel kører, HDMI-enable=1, ingen efterladt daemon. Bemærk:
      nodm valgte vt7 denne gang (ikke boot-standardens vt8) — tjek altid aktiv
      VT mod X' vt. Med `_exit`-fixet kørte ingen display-dans, så strøm-cyklus
      er sandsynligvis ikke nødvendig — TV-bekræftelse afventer brugeren.

### M4 — integration og distribution

- [ ] `gpu_setup.sh`: byg `gles_daemon` + `frontend.py` fra repoet (cross) og
      distribuér dem; stop med at hente byggede binærer fra boks 1.
- [ ] Opdatér `devuan/gpu/README.md` (operationskort) og DOKUMENTATION.md §5.15
      med daemon-arkitekturen + cross-byggelinjen.
- [ ] Overvej udvidelser (ikke v1): scaling, select()-event-loop i daemonen,
      tastatur-input til frontenden, flere scener.

### M4a — `eglplatform_x11`-prototype (24. aug 2026, BROWSER-VEJE §2.A)

`devuan/gpu/eglplatform_x11/` — en rigtig libhybris EGL-platform: PVR renderer
offscreen i gralloc-buffere, og platformens `queueBuffer` præsenterer dem i et
X-vindue via XPutImage (RGB565-pakning for X' 16-bit-visual). Bygges på boksen
(`build_box.sh`; armhf-X11-headere mangler på laptoppen). Testklient:
`test_client_x11.cpp` (X-vindue + `EGL_PLATFORM=x11` + cos-scene).

**VERIFICERET (24. aug 2026):** 640x360-vindue viser cos-mønsteret på fb0 —
100 unikke farver (hvid 0xFFFF, sort, varme cos-farver 0xFF36 osv.), animation
bekræftet (6.208 px forskel mellem to dumps midt i kørslen), ~9 fps @ 640x360,
GL 3.1 PowerVR G6110, aktiv VT og HDMI urørt bagefter. `ws_module`-kontrakten
(A3) holder — libEGL dlopen'er `eglplatform_x11.so` og kalder `ws_module_info`.

**To målte fælder (begge fikset i platformen):**
1. **Hybris' EGL-init skifter aktiv VT væk fra X' VT (målt: →10), og fbdev-X
   kopierer KUN shadow→fb0, når X' VT er aktiv** — ellers når al tegning aldrig
   skærmen (xwininfo viser IsViewable, men fb0 er urørt). Fix: `ensure_x_vt()`
   finder Xorgs VT via `/proc/*/cmdline` (bemærk: NUL-adskilte argumenter — almindelig
   strstr stopper ved første NUL!) og chvt'er via ioctl, kaldt ved første present.
   NB: hverken `popen`/`pgrep` virker i hybris-processer (ødelagt environ →
   execve-EFAULT — samme fælde som `system()`).
2. **Tegning fra en ANDEN X-forbindelse end vinduets egen når ikke fb0 på
   denne server** (uanset settle/Expose). Fix: klienten sender sit `Display*`
   som EGL-native-display (`eglGetDisplay(dpy)`); platformens `GetDisplay`
   gemmer det, og `present()` tegner via klientens forbindelse.

**Firefox-forsøg (24. aug 2026, BROWSER-VEJE eksperiment 2):** `MOZ_X11_EGL=1`
+ `EGL_PLATFORM=x11` + shims lader Firefox' `glxtest` loade vores libEGL og
Android-EGL-kæden, men `eglGetDisplay` fejlede i glxtest's proces
(EGL_BAD_DISPLAY, logd "eglGetDisplay:218 error 300c"). **LØST:** rodårsagen
var, at glxtest henter kerne-EGL-funktioner gennem `eglGetProcAddress`
(ikke dlsym), og platformens `ws_eglGetProcAddress` returnerede NULL for
kerne-EGL-navne → Android-loaderens interne funktioner vandt. Fix i
`eglplatform_x11.cpp`: `ws_eglGetProcAddress` videresender kerne-EGL-navne til
wrapperens egne eksporter (+ stubs for `eglQueryDeviceStringEXT`/
`eglQueryDisplayAttribEXT`); glxtest-binæren patchet (dybde-tjek 24→16, X er
16-bit; backup `/root/glxtest.orig`). `glxtest` melder nu PowerVR Rogue G6110 /
GLES 3.1 / TEST_TYPE=EGL. **Ny blokering (fuld Firefox):** WebRender-hardware-
kontekst fejler (0x300c: create rammer ikke wrapperen; 0x3000: create+MakeCurrent
virker, Init fejler). Detaljer: `devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md`.
Diagnose-værktøjer: `dlsym_trace.c` (i stykker — brug ikke), `dlopen_egl_test.cpp`,
`egl_display_probe.cpp`, `egl_getproc_probe2`, `ff_egl_mimic`,
`android_internal_probe`, `epoxy_mimic` (kilde i `/tmp/epoxy_mimic.c`).

## Regler og fælder (målt/arvet — ikke gæt)

1. **X stoppes under brug** — daemonen blitter til fb0, så skærm-output kolliderer
   med X (README-regel 1).
2. **Strøm-cyklus bagefter** — HDMI-transmitteren kan ikke vækkes af software
   (DOK §5.15).
3. `cma=128M` skal stå på cmdlinen, ellers EPERM ved 2. CMA-buffer (DOK §5.15).
4. `LD_PRELOAD=/root/system_shim.so` er nødvendig mod glibc-2.41-EFAULT i
   hybris-processen (DOK §5.15).
5. `/dev/graphics/fb*` forsvinder ved hver boot — `gpu_up.sh` genskaber dem.
6. fb0's bpp/stride kan variere ved boot (EDID-race) — myinit normaliserer;
   daemonen læser var-info ved start.
7. Ikke kør tunge installationer på boksen (brownout, DOK §5.14).
8. Racerbetingelsen frontend↔daemon om fb0 løses i v1 af den synkrone protokol og
   adskilte rects — dokumenteret, ikke løst.
9. XPutImage til et vindue kræver `ZPixmap` (2) i `XCreateImage` — `XYPixmap`
   (1) → SIGSEGV i libX11/libc (målt med gdb, M2b).
10. X' fbdev-driver maler ikke root-baggrund ved opstart — gamle fb0-blits
    bliver stående på skærmen (målt, M2b; brug clearroot før visuelle tests).
11. openbox/LXDE flytter nye vinduer — verificér position med `xwininfo -id`
    eller `-root -tree`; stol ikke på egne koordinater (målt, M2b).
12. scp over en kørende eksekverbar fejler ("dest open Failure") — dræb
    processen først (målt, M2b).

## Åbne spørgsmål

- Den eksakte link-linje (M0 fastlægger den; dette dokument opdateres).
- FBO-egenskab på PVR-stakken: forventet core-ES3 (renderer melder 3.1); fallback
  er `GL_OES_framebuffer_object` hvis FBO-init fejler.
- Tekst oven på daemonens rect: v1-svar er at frontenden tegner sin UI bagefter
  hver render, eller placerer tekst uden for rect'en.
