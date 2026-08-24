# GPU-stakken — operationskort (start her i en ny session)

Kort over hvad der ligger HVOR på boksen, og hvordan man kører en GPU-session.
Historien og beviserne: DOKUMENTATION.md §5.13-5.15. Hverdagssprog-versionen:
`GRAFIK-FORKLARET.md`.

## Boksens runtime-layout (boks 1, aug 2026)

| Sti | Indhold |
|---|---|
| `/usr/local/share/libhybris/system.img` | Vendors Android-image (198 MB) — blobs, bionic-libs, logd, servicemanager, pvrsrvctl, wifi-firmware |
| `/system` | Loop-mount af ovenstående (monteres automatisk af myinit — ellers dør wifi) |
| `/vendor` | Symlink → `/system/vendor` |
| `/opt/hybris/` | Hybris-broerne fra `vendor_root/usr/local/lib` + **usage-patchet** `libhybris-hwcomposerwindow.so.1.0.0` (0x1800→0x1000, backup `.orig`) + test-binærerne |
| `/usr/local/lib/libhybris/` | Kopi af eglplatformerne (hårdkodet sti i broen) |
| `/usr/local/include/` | Vendors headere (til at bygge på boksen) |
| `/root/test_triangle` | Vores kompilerede GLES-demo (500 frames verificeret) |
| `/root/system_shim.so` | LD_PRELOAD-shim mod glibc-2.41-EFAULT i system() |
| `/root/gpu_up.sh` | Starter stakken (logd + servicemanager + pvrsrvctl + /dev/graphics-symlinks) |
| `/root/hwc_test.log`, `ion.log` m.fl. | Fejlsøgningsrester (kan slettes) |
| `/dev/graphics/fb*` | Symlinks til /dev/fb* — forsvinder ved hver boot, `gpu_up.sh` genopretter |

Boksen har desuden gcc/g++/gdb/strace (kun til at bygge/debugge — NYE bokse behøver
dem ikke, `gpu_setup.sh` henter de færdige binærer).

## Sådan kører man en GPU-session

```bash
ssh -i ~/.ssh/geekbox_key root@<ip>          # find ip: devuan/find_box.sh
ssh -i ~/.ssh/geekbox_key root@<ip> 'sh /root/gpu_up.sh'
ssh -i ~/.ssh/geekbox_key root@<ip> 'service nodm stop'     # X SKAL stoppes
ssh -i ~/.ssh/geekbox_key root@<ip> \
  'LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=hwcomposer /root/test_triangle'
# bagefter: service nodm start + STRØM-CYKL boksen (HDMI vågner kun sådan)
```

Reglerne (målt, ikke gættet):
1. **hwc-præsentation og X kan ikke deles om skærmen.** GLES offscreen kan sagtens
   køre mens X kører — kun skærm-output kolliderer.
2. **Efter en GPU-session: strøm-cyklus.** Display-dansen kan ikke vække
   HDMI-transmitteren (kernen tror den sender; TV'et får intet).
3. Kør ikke tunge installationer på boksen (brownout-historien, DOK §5.14) — og brug
   aldrig "5V 2A"-adapteren.

## Bygning — cross på laptoppen (M0, aug 2026)

`devuan/gpu/build.sh` bygger `test_triangle` + `system_shim.so` til armhf med
`g++-arm-linux-gnueabihf` mod `vendor_root`-libs — verificeret: identisk adfærd på
boks 1 (500 frames, samme GL_VERSION/GL_RENDERER). Kør:  `devuan/gpu/build.sh`
(output i `devuan/gpu/bin/`). Boks 1 er ikke længere det eneste byggehost.

## GLES-daemon (M1, aug 2026)

`gles_daemon` lytter på unix-socket `/tmp/gles.sock`, renderer offscreen (FBO) og
blitter til fb0. Protokol: JSON-linjer — `ping`, `fb`, `scenes`, `render`
(scene/rect/phase), `clear` (rect/color), `quit`. Testklient:
`devuan/gpu/socktest.py`. Detaljer + fund: `GLES-DAEMON-PLAN.md`.

```bash
ssh -i ~/.ssh/geekbox_key root@<ip> 'service nodm stop'
ssh -i ~/.ssh/geekbox_key root@<ip> 'sh /root/gpu_up.sh'
ssh -i ~/.ssh/geekbox_key root@<ip> \
  'LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=hwcomposer /root/gles_daemon &'
ssh -i ~/.ssh/geekbox_key root@<ip> 'python3 /root/socktest.py'
ssh -i ~/.ssh/geekbox_key root@<ip> \
  'python3 /root/frontend.py --frames 30 --fps 5'   # UI + animeret scene
# bagefter: service nodm start (daemonen præsenterer ikke via hwc)
```

NB: daemonen patcher hybris-wrapperens tomme `_glReadPixels`-slot — uden patchen
er `glReadPixels` et NULL-kald → SIGSEGV → exit(42) (målt med strace+gdb).

## X-vindue-demo (M2b, aug 2026) — X kører, demoen i et vindue

`window_demo.py` (ctypes + libX11, ingen tkinter/PIL) åbner et vindue i X,
snakker med daemonen over socketten og viser GLES-scenen i vinduet. **X stoppes
IKKE** — daemonen renderer offscreen og returnerer pixels (`frame`-kommandoen)
i stedet for at blitte til fb0.

```bash
ssh -i ~/.ssh/geekbox_key root@<ip> 'sh /root/gpu_up.sh'
ssh -i ~/.ssh/geekbox_key root@<ip> \
  '(LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=hwcomposer nohup /root/gles_daemon >/root/gles_daemon.log 2>&1 &)'
ssh -i ~/.ssh/geekbox_key root@<ip> \
  'su -s /bin/sh kristian -c "cd /home/kristian && DISPLAY=:0 python3 window_demo.py --frames 60 --fps 10"'
# bagefter: pkill -9 -x gles_daemon + chvt 8 (quit er også sikkert — _exit-fix)
```

`frame` returnerer en JSON-header + rå pixels: `fmt="rgba8"` (4 B/px) eller
`fmt="rgb565"` (2 B/px, little-endian R5G6B5) — rgb565 er C-pakket i daemonen og
giver ~10 fps målt (60 frames på 6,0 s, 640x360). Status 24. aug 2026:
implementeret, kørt på boks 1 uden crash og **skærm-verificeret** (vindue →
fb0; `xwininfo -id` IsViewable + `fbdump` midt i kørslen viser mønster og tekst
i vindue-området) — detaljer og fund: `GLES-DAEMON-PLAN.md` M2b. Vigtigste
fælder: XPutImage kræver `ZPixmap` (2) i `XCreateImage` (1 → SIGSEGV); openbox
flytter vinduet (tjek med `xwininfo`); X maler ikke root-baggrund ved opstart
(kør `diagnostik/clearroot.c` før visuelle tests); daemonens start skifter
aktiv VT (kør `chvt 8` efter start); daemonens `quit`-exit-dans kunne slå
HDMI-displayet fra og efterlade en sort skærm med X i live — **fikset 24. aug**
via `_exit()` i daemonen (ingen atexit-dans; verificeret: VT og HDMI-enable
urørte ved quit); dræb en kørende daemon før scp.

## Python-projektet (i gang — aug 2026)

## eglplatform_x11-prototype (24. aug 2026) — GLES ind i et X-vindue

BROWSER-VEJE §2.A i udført form: `devuan/gpu/eglplatform_x11/` er en rigtig
libhybris-EGL-platform, der renderer via PVR (offscreen, gralloc) og
præsenterer i et X-vindue via XPutImage. Bygges på boksen (`build_box.sh`),
testes med `test_client_x11` (`EGL_PLATFORM=x11`). Verificeret: cos-scenen
viser på fb0 i 640x360-vinduet, ~9 fps, GL 3.1. To vigtige fælder (fikset):
hybris' EGL-init skifter aktiv VT væk fra X (platformen chvt'er tilbage ved
første present — fbdev-X viser kun indhold, når dens VT er aktiv), og tegning
skal gå gennem vinduets egen X-forbindelse (klienten sender sit Display* som
EGL-native-display). Detaljer: `GLES-DAEMON-PLAN.md` M4a.

Arkitektur: Python-frontend (tegner UI direkte på `/dev/fb0`, som `fb_overscan.py`)
↔ unix-socket ↔ GLES-daemon (C, skelet = `test_triangle.cpp`), der renderer offscreen
og blitter til fb0 — eller returnerer pixels til et X-vindue (M2b, ovenfor).
Kommandoer over socketten i JSON-linjer. X stoppet kun i kiosk-tilstanden (frontend.py).

**Plan og status: `GLES-DAEMON-PLAN.md`** — beslutninger, milestones (M0-M4),
testcyklus og fælder. Vigtigste nye beslutning (23. aug 2026): alt bygges CROSS på
laptoppen (`g++-arm-linux-gnueabihf` mod `vendor_root`-libs), ikke på boksen —
boks 1 er ikke længere det eneste byggehost, og nye bokse behøver aldrig gcc.

Første konkrete skridt:
1. `devuan/gpu/gles_daemon.c`: tag `test_triangle.cpp`, erstat animations-loopet med
   en socket-lytter; render scener efter kommandoer; blit til fb0.
2. `devuan/gpu/frontend.py`: minimal UI på fb0 (tekst + rammer) med en socket-klient.
   `devuan/gpu/window_demo.py`: samme scene i et X-vindue (M2b, X kører).
3. Testcyklus: stop X → start daemon → start frontend → kommandoer → strøm-cyklus.

## Ny boks i samme tilstand

```bash
sudo devuan/testflash.sh                 # image har cma=128M + myinit-mount
devuan/gpu/gpu_setup.sh <ny-ip>          # system.img + broer + færdige binærer
# strøm-cykl — se DOK §5.15 "Ny boks i samme tilstand"
```
