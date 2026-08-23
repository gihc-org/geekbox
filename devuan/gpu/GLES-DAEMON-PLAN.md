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
- [ ] M1: `gles_daemon.c`
- [ ] M2: `frontend.py`
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

### M1 — `devuan/gpu/gles_daemon.c` (README-skridt 1)

- [ ] Kopiér `test_triangle.cpp`'s init (hwc-modul, vindue, EGL, GLES2-shaderen).
- [ ] Erstat animations-loopet med en unix-socket-lytter (`/tmp/gles.sock`; fjern
      stale socket ved start; ryd op ved SIGTERM/SIGINT).
- [ ] Minimal dependency-fri JSON-parser (objekter: string/number/array — nok til
      protokollen).
- [ ] Kommandoer: `ping`, `fb` (skærmgeometri), `render` (scene, rect, params),
      `clear` (fyld rect med farve), `quit`. Fejl svares med `{"ok":false,...}`.
- [ ] Scene v1: `triangle` = cos-mønster-shaderen fra `test_triangle`.
- [ ] FBO 1920x1080 RGBA8; `glReadPixels` → konvertering til fb0-format → mmap-blit.
- [ ] Accept: `ping`/`fb` svarer uden GPU-risiko; `render`+`clear` giver korrekt
      fb0-indhold (verificeres visuelt i M3).

### M2 — `devuan/gpu/frontend.py` (README-skridt 2)

- [ ] Python 3, kun stdlib. Åbn `/dev/fb0` O_RDWR + mmap; læs var-info (arvet fra
      `fb_overscan.py` — samme ioctl-struktur).
- [ ] Tegn: fyld, rammer, tekst via indlejret 5x7-bitmapfont (ingen afhængigheder).
- [ ] Socket-klient: forbind, send JSON-linjer, læs svar.
- [ ] Demo-loop: baggrund + ramme + titel/statuslinjer, animeret fase-sweep af
      `triangle`-scenen i N frames, derefter `clear`.
- [ ] Flag: `--fb`, `--socket`, `--rect WxH+X+Y`, `--frames`, `--fps`, `--no-gles`
      (UI-test uden daemon).
- [ ] Accept: kører på boksen uden X; fb0 dumpet til PNG viser rammer, tekst og
      GLES-scene (metoden fra DEBUG-SORT-SKAERM).

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
