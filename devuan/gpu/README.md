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

## Python-projektet (i gang — aug 2026)

Arkitektur: Python-frontend (tegner UI direkte på `/dev/fb0`, som `fb_overscan.py`)
↔ unix-socket ↔ GLES-daemon (C, skelet = `test_triangle.cpp`), der renderer offscreen
og blitter til fb0. Kommandoer over socketten i JSON-linjer. X stoppet mens det kører.

**Plan og status: `GLES-DAEMON-PLAN.md`** — beslutninger, milestones (M0-M4),
testcyklus og fælder. Vigtigste nye beslutning (23. aug 2026): alt bygges CROSS på
laptoppen (`g++-arm-linux-gnueabihf` mod `vendor_root`-libs), ikke på boksen —
boks 1 er ikke længere det eneste byggehost, og nye bokse behøver aldrig gcc.

Første konkrete skridt:
1. `devuan/gpu/gles_daemon.c`: tag `test_triangle.cpp`, erstat animations-loopet med
   en socket-lytter; render scener efter kommandoer; blit til fb0.
2. `devuan/gpu/frontend.py`: minimal UI på fb0 (tekst + rammer) med en socket-klient.
3. Testcyklus: stop X → start daemon → start frontend → kommandoer → strøm-cyklus.

## Ny boks i samme tilstand

```bash
sudo devuan/testflash.sh                 # image har cma=128M + myinit-mount
devuan/gpu/gpu_setup.sh <ny-ip>          # system.img + broer + færdige binærer
# strøm-cykl — se DOK §5.15 "Ny boks i samme tilstand"
```
