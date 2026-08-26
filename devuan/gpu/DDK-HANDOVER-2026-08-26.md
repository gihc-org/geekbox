# DDK-sporet — handover 26. aug 2026 (~02:5x)

> Læs dette dokument + `GPU-FAULT-GENNEMBRUD-SESSION-NOTAT-2026-08-26.md`
> (sektionen "DDK-sporet (research)") og fortsæt derfra.
> Mål: få Subway Surfers (poki.com) til at spille i firefox-esr på boks 1
> (192.168.0.188).

## Hvad DDK er

**DDK = Device Driver Kit** — Imagination Technologies' PowerVR-driverpakke:
kernelmodul (`pvrsrvkm`), userspace (`libEGL_POWERVR_ROGUE.so`,
`libGLESv2_POWERVR_ROGUE.so`, `libIMGegl.so`, `libsrv_um.so`, `libusc.so`,
`libglslcompiler.so`), USC-shaderkompiler og GPU-firmware. Vores boks kører
DDK 1.4@3632227 (32-bit userspace, arm64 kernel 3.10.0).

## Situation

- Subway Surfers' frys er forklaret: Unity-shaders kræver
  `#extension GL_EXT_draw_buffers : require` (MRT), og DDK 1.4's
  GLSL-kompiler afviser direktivet ("Extension GL_EXT_draw_buffers not
  supported") — MÅLT med `shader_ext_test.c`/`trivial_test.c`, selvom
  GL_EXT_draw_buffers står i GL_EXTENSIONS.
- WebGL1-tvang + udvidelsesliste-patch afkræftet (02:1x): kompileren har en
  hardkodet extension-liste; draw_buffers står ikke på den i 1.4.
- Buffer-fixet (retire-alle, md5 4ba7a90d) løser GPU-MMU-fault/reset-sporet på
  stress-siden, men IKKE spillets shader-blokade.

## Gennembrud i research (26. aug 02:3x–02:5x)

**DDK 1.5@3830101 findes som KOMPLET 32-bit stak + arm64 KM med samme
vermagic som vores kernel** — og 1.5's GLSL-kompiler kender
`GL_EXT_draw_buffers` (strings-målt; 1.4's gør ikke).

Kilde: `leddaz-dump-stash/android_rk3368_box_dump` (GitHub), branch
`rk3368_box-userdebug-6.0.1-MXC89K-user.root.20181130.004438-test-keys`
(Android 6.0.1 rk3368_box, produkt ACON1-G, bygget 30. nov 2018).

Verificeret:
- 1.5-UM er 32-bit ARM (matcher vores userspace); 1.5-KM er aarch64 og siger
  `Rogue_DDK_Android rogueddk 1.5@3830101`, vermagic
  `3.10.0 SMP preempt mod_unload aarch64` (samme som vores nuværende 1.4-ko).
- Boksens Android 5.1.1-/system har ALLE 1.5-afhængigheder
  (libc++/libunwind/libsync/libcutils/libhardware/libz/libm/libdl/libc).
- DDK 1.8 (Rogue 1.8.RTM@4610191) er 4.4-kernel-æra (Firefly) — ikke fundet
  offentligt til 3.10; 1.5 er det konkrete kandidat-spring.

## Filer (lokalt på laptop: /tmp/ddk18/, /tmp/ddk16/)

1.5-sæt (fra dumpet; md5 under):

```text
system/vendor/lib/egl/libEGL_POWERVR_ROGUE.so      bfe1ec351d1cbb45933c7aa8f4fe15ce
system/vendor/lib/egl/libGLESv1_CM_POWERVR_ROGUE.so 33dbb95c395bc421fd371745dbaffaf0
system/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so   f73c7e643713b2a2721f49da080e48a0
system/vendor/lib/libIMGegl.so                     62568581cbb82468d5ab5d2ddf5c0f93
system/vendor/lib/libsrv_um.so                    e175098aa471fb8629ec0a28e5a96432
system/vendor/lib/libusc.so                       22dface2e4ccfa3cd602373abf70d388
system/vendor/lib/libglslcompiler.so              3eda681e2eecc4c0a15d5b529c4119ff
system/vendor/lib/libufwriter.so                  5c836927f65dac642e011098af34b2d9
system/vendor/lib/libpvrANDROID_WSEGL.so          a12673eb43772a0300c38c5503d41fda
system/vendor/lib/libcreatesurface.so             b962a2f9f30d350eebc8e3f8d435f672
system/vendor/lib/libPVRScopeServices.so          d6e1d51d74e25a484541abac733c117f
system/vendor/lib/libPVROCL.so                    249cc54da6e2a8918b44a5936e5790d3
system/vendor/lib/liboclcompiler.so               1b3b5547000fcf0ec3508692e4b29025
system/vendor/lib/hw/gralloc.rk3368.so            380658e4bc779afb883822de46eff186
system/vendor/lib/hw/memtrack.rk3368.so           62a953e3af479e767c9fde47033a7d42
system/vendor/bin/pvrsrvctl                        4e4aa6f6490e6c645c791831271badbc
system/vendor/bin/pvrtld                           5e7d51b2144cbac82847a14130dd75b3
system/lib/modules/pvrsrvkm.ko                     8338734fe08b8184498ce387472499d6
system/vendor/lib/egl/egl.cfg                      3be7b8b182ccd96e48989b4e57311193
```

1.4-reference (nuværende/restore, fra geekboxzone-mirror — identisk med
boksens filer; md5):

```text
libEGL_POWERVR_ROGUE.so     d32ef811909c8218dd5f3b3c60d5182c
libGLESv1_CM_POWERVR_ROGUE.so f393cce536a9a75ba12426fcfd0402ae
libGLESv2_POWERVR_ROGUE.so  9121bfa0692cb9c38dbfa2033ef54160
libIMGegl.so                b31766a6c2c9b599828e70ceded66a36
libsrv_um.so                9dc3db08f1f256066bf3e448fbb88081
libusc.so                   c0c327655b8a25923215b99b40d99f07
libglslcompiler.so          72b1de98dceb1c1db5405341a39d2214
```

Download-URL'er (genhent hvis /tmp er ryddet):

```bash
BR="rk3368_box-userdebug-6.0.1-MXC89K-user.root.20181130.004438-test-keys"
curl -sSL -o /tmp/ddk18/libGLESv2_POWERVR_ROGUE.so \
  "https://raw.githubusercontent.com/leddaz-dump-stash/android_rk3368_box_dump/$BR/system/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so"
# ... samme mønster for hver fil; stier i listen ovenfor.
# 1.4-reference:
curl -sSL -o /tmp/ddk16/libGLESv2_POWERVR_ROGUE.so \
  "https://raw.githubusercontent.com/geekboxzone/mmallow_vendor_rockchip_common/geekbox/gpu/libG6110/G6110_32/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so"
```

## Plan for prøveinstallation (næste session — IKKE startet)

> Sikkerhedsreglerne nederst gælder HELE tiden. Boksen har ikke nødvendigvis
> GPU'en oppe lige nu (oprydnings-tilstand) — start med at tjekke.

1. **Tjek boksens tilstand** (læse-kun):
   ```bash
   ssh -i ~/.ssh/geekbox_key root@192.168.0.188 'ls -la /dev/pvrsrvkm; ls /system/vendor/lib/egl/; grep pvrsrvkm /proc/modules'
   ```
2. **Backup nuværende DDK 1.4 på boksen**:
   ```bash
   ssh -i ~/.ssh/geekbox_key root@192.168.0.188 \
     'mkdir -p /root/ddk14-backup/egl /root/ddk14-backup/lib /root/ddk14-backup/hw; \
      cp -a /system/vendor/lib/egl/libEGL_POWERVR_ROGUE.so /system/vendor/lib/egl/libGLESv1_CM_POWERVR_ROGUE.so /system/vendor/lib/egl/libGLESv2_POWERVR_ROGUE.so /root/ddk14-backup/egl/; \
      cp -a /system/vendor/lib/libIMGegl.so /system/vendor/lib/libsrv_um.so /system/vendor/lib/libusc.so /system/vendor/lib/libglslcompiler.so /root/ddk14-backup/lib/; \
      cp -a /system/vendor/lib/hw/gralloc.rk3368.so /system/vendor/lib/hw/memtrack.rk3368.so /root/ddk14-backup/hw/; \
      cp -a /system/lib/modules/pvrsrvkm.ko /root/ddk14-backup/pvrsrvkm.ko; \
      md5sum /root/ddk14-backup/egl/* /root/ddk14-backup/lib/* /root/ddk14-backup/pvrsrvkm.ko'
   ```
3. **Læg 1.5-filerne over** (scp fra laptop; de vigtigste: egl-libs +
   libIMGegl/libsrv_um/libusc/libglslcompiler + pvrsrvkm.ko):
   ```bash
   scp -i ~/.ssh/geekbox_key /tmp/ddk18/libEGL_POWERVR_ROGUE.so /tmp/ddk18/libGLESv1_CM_POWERVR_ROGUE.so /tmp/ddk18/libGLESv2_POWERVR_ROGUE.so \
     root@192.168.0.188:/root/ddk15-egl/
   # ... scp resten til /root/ddk15/
   ```
4. **Skift filerne** (og gem md5-før/efter i bevis-log):
   ```bash
   ssh -i ~/.ssh/geekbox_key root@192.168.0.188 'cp /root/ddk15-egl/* /system/vendor/lib/egl/; \
     cp /root/ddk15/libIMGegl.so /root/ddk15/libsrv_um.so /root/ddk15/libusc.so /root/ddk15/libglslcompiler.so /system/vendor/lib/; \
     cp /root/ddk15/gralloc.rk3368.so /root/ddk15/memtrack.rk3368.so /system/vendor/lib/hw/; \
     cp /root/ddk15/pvrsrvkm.ko /system/lib/modules/; \
     md5sum /system/vendor/lib/egl/* /system/vendor/lib/libglslcompiler.so /system/lib/modules/pvrsrvkm.ko'
   ```
5. **KM indlæses:** hvis pvrsrvkm er loadet: `rmmod pvrsrvkm` (hvis busy:
   genstart boksen — myinit loader ko'en fra /system). Tjek dmesg efter
   insmod/reboot: `dmesg | grep -E "PVR_K|pvrsrvkm|Rogue"` STRAKS (ringen
   roterer på få minutter).
6. **Start stakken:** efter genstart af boksen (hvis den genstartes):
   `patch_android_bindapi.sh` + `patch_driver_minor.sh` (fra laptop, repoet) +
   `sh /root/gpu_up.sh` (boks) + genopbyg /tmp-værktøjer (scp+gcc;
   gl_reset_probe med `-Wl,-rpath-link,/opt/hybris`).
7. **Test 1 — kompileren:** byg+kør `trivial_test.c`/`shader_ext_test.c` på
   boksen:
   ```bash
   gcc -o /tmp/shader_ext_test shader_ext_test.c -I/usr/local/include -L/opt/hybris \
     -Wl,-rpath-link,/opt/hybris -lEGL -lGLESv2 -lhybris-common -ldl -lrt -lm
   /tmp/shader_ext_test   # forvent: ES2/ES3 med GL_EXT_draw_buffers = OK
   ```
8. **Test 2 — WebGL-version:** `GL_VERSION` skal sige
   `OpenGL ES 3.1 build 1.5@3830101` (webgl_check / about:support).
9. **Test 3 — spillet:** start_game.sh mod poki.com Subway Surfers. Forvent:
   Unity-shaders kompilerer, spillet præsenterer frames (present #2+ → flere),
   ingen "WebGL context was lost".
10. **Hvis 1.5 går i stykker:** restore = kopier 1.4-filerne tilbage fra
    /root/ddk14-backup (og pvrsrvkm.ko), genstart, kør gpu_up.sh. Boksen er
    ellers urørt.

## Afhængigheder og risici (målt/vurderet)

- **KM-load (MODVERSIONS):** ukendt om kernen har CONFIG_MODVERSIONS (ingen
  /proc/config.gz). Hvis 1.5-ko'en nægter at loades med symbol-versionering:
  byg 1.5-KM fra `geekboxzone/mmallow_kernel` (Android 6.0-kilde, 3.10) mod
  vores kernel — eller test UM 1.5 mod KM 1.4 (bridge-ABI er sandsynligvis
  tæt, men ustøttet). Første forsøg: præbygget ko (samme vermagic).
- **Android 5.1 vs 6.0-libs:** 1.5-libs er bygget mod Android 6.0-bionic,
  men afhænger kun af stabile libs der findes i 5.1.1 (verificeret).
  Hvis noget mangler: kopier den pågældende lib fra dumpet
  (`system/lib/libc++.so` osv.) — men kun hvis nødvendigt.
- **gralloc:** 1.5-gralloc.rk3368.so medfølger; start med den. Hvis
  hwcomposer/gralloc-integrationen brokker sig, behold 1.4-gralloc'en
  (gralloc0-ABI er stabil).
- **libsrv_init.so findes ikke i dumpet (32-bit)** — behold 1.4's (bruges af
  pvrsrvctl-init; ingen 1.5-modpart nødvendig for GLES-stien).
- **libufwriter/libPVROCL/liboclcompiler er IKKE i GLES-afhængighedskæden**
  (readelf-verificeret) — kan udskydes; tag dem med hvis 1.5-egl/libsrv_um
  brokker sig over manglende symboler.

## Sikkerhedsregler (målt — overtræd ikke)

- ALDRIG `MOZ_GL_SPEW=1` eller body-rød-testen; ingen
  `XGetImage(root)`/`dd if=/dev/fb0` under load (fb-read-wedge i D-state →
  sysrq-b-genstart).
- dmesg-ringen roterer på få minutter (syscall-403-flood) — PVR-bevis skal
  gemmes STRAKS.
- Genstart boksen efter ~8 Firefox-opstarter; stop ved høj load med
  `pkill -9 -x firefox-esr` (ALDRIG `-f firefox` — dræber SSH-skallet).
- Tjek VT=tty7 og HDMI=1 efter hvert forsøg.
- Efter boks-genstart: `patch_android_bindapi.sh` + `patch_driver_minor.sh`
  (laptop) + `sh /root/gpu_up.sh` (boks) + genopbyg /tmp-værktøjerne.

## Næste skridt (prioriteret)

1. Prøveinstallation af DDK 1.5 (plan ovenfor) → verificér
   `GL_EXT_draw_buffers`-kompilering + spillet.
2. Hvis KM-load fejler: byg 1.5-KM fra mmallow_kernel (3.10) — kræver
   aarch64-krydskompiler + kernen mod samme config.
3. Hvis 1.5 virker: gem beviser (GL_VERSION, shader_ext_test-output,
   spil-log) i `devuan/gpu/beviser/`, beslut om blobberne skal ind i repoet,
   opdater DOK/TODO/HAANDBOG + commit.
4. Fortsæt separat: buffer-fix/kadence (stress-siden) — kun hvis spillet nu
   virker og ydeevnen er for lav.

## God start i en ny session

> "Læs `devuan/gpu/DDK-HANDOVER-2026-08-26.md` og `devuan/gpu/GPU-FAULT-
> GENNEMBRUD-SESSION-NOTAT-2026-08-26.md` (DDK-sektionen) og fortsæt derfra.
> Vi skal prøveinstallationere DDK 1.5@3830101 på boks 1 (192.168.0.188)
> for at få GL_EXT_draw_buffers i shader-kompileren og dermed Subway Surfers
> til at køre. Backup + restore ligger klar i handoveren."
