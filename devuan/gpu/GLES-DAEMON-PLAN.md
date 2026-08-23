# GLES-daemon + Python-frontend — arbejdsplan (aug 2026)

Plan for TODO-punktet "Python-frontend + GLES-daemon" (TODO.md): Python tegner UI
direkte på `/dev/fb0`, taler med en C-daemon over en unix-socket (JSON-linjer), og
daemonen renderer GLES offscreen og blitter resultatet til fb0. X stoppes under
brug; strøm-cyklus bagefter (reglerne i DOK §5.15). Arkitektur-kortet:
`devuan/gpu/README.md`.

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
- [ ] M3: testcyklus på boksen
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

### M0 — baseline: test_triangle bygger cross ✅ (23. aug 2026)

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

### M1 — `devuan/gpu/gles_daemon.c` ✅ (23. aug 2026)

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

### M2 — `devuan/gpu/frontend.py` ✅ (23. aug 2026)

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

### M3 — testcyklus på boks 1

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

- [ ] Daemon starter, `ping`/`fb` svarer; frontend tegner UI på fb0.
- [ ] `triangle`-scenen vises korrekt i rect'en (fb0-dump verificeret visuelt).
- [ ] X kan startes igen efter strøm-cyklus.

### M4 — integration og distribution

- [ ] `gpu_setup.sh`: byg `gles_daemon` + `frontend.py` fra repoet (cross) og
      distribuér dem; stop med at hente byggede binærer fra boks 1.
- [ ] Opdatér `devuan/gpu/README.md` (operationskort) og DOKUMENTATION.md §5.15
      med daemon-arkitekturen + cross-byggelinjen.
- [ ] Overvej udvidelser (ikke v1): scaling, select()-event-loop i daemonen,
      tastatur-input til frontenden, flere scener.

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

## Åbne spørgsmål

- Den eksakte link-linje (M0 fastlægger den; dette dokument opdateres).
- FBO-egenskab på PVR-stakken: forventet core-ES3 (renderer melder 3.1); fallback
  er `GL_OES_framebuffer_object` hvis FBO-init fejler.
- Tekst oven på daemonens rect: v1-svar er at frontenden tegner sin UI bagefter
  hver render, eller placerer tekst uden for rect'en.
