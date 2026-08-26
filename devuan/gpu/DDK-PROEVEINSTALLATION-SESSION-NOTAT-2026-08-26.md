# DDK 1.5-prøveinstallation — session-notat 26. aug 2026 (~03:0x–04:0x)

## Aftaler og beslutninger

- [udført] **Prøveinstallation af DDK 1.5@3830101 gennemført og rullet tilbage til 1.4**
  (26. aug, 03:1x–04:0x). Resultat: **1.5-userspace hænger boksens indbyggede KM HÅRDT
  (hard lockup ved EGL-init) — DDK 1.5 er ikke anvendelig på denne boks uden en nyere
  indbygget KM.** To uafhængige wedges (første gang på korrupt filsystem efter egen
  cp-fejl; anden gang på RENT filsystem + 6.0-libc + kontrolleret test) — konklusionen
  er derfor solid.
- [målt] **KM'en er bygget ind i kernen, ikke et .ko-modul:** tom `/proc/modules`,
  kallsyms har PVRSRVDriverProbe/PVRSRVConnectKM, `/sys/module/pvrsrvkm/` har kun
  parameters+uevent (ingen initstate/holders/refcnt), `rmmod` siger "Module unloading
  is not supported", `insmod` af 1.4-ko'en giver "Invalid module format", og bootloggen
  viser `PVR_K: sys.gpvr.version=Rogue L 0.22` ved 2,7 s (kernel-init). **Handoverens
  antagelse om at bytte `pvrsrvkm.ko` er derfor ugyldig — en 1.5-KM kræver en kernel-
  rebuild.**
- [målt] **1.5-userspace kræver Android 6.0's bionic-libc:** `libsrv_um.so` (1.5)
  linker IKKE mod boksens 5.1-libc — manglende `__register_atfork@LIBC` (findes kun i
  6.0-libc). 5.1-libc har kun `__cxa_atexit`/`atexit`. Hentet 6.0-libc fra dumpet
  (`system/lib/libc.so`, md5 99dcc69f) — derefter loadede libs'ene.
- [målt] **1.5-dumpets `pvrsrvctl` er 64-bit AArch64** (kun 64-bit variant i
  vendor/bin; `libsrv_init.so` findes slet ikke i 32-bit-form i dumpet) → kan ikke
  exec'es på boksens 32-bit userspace ("required file not found", ingen linker64).
  1.4-pvrsrvctl (32-bit) segfault'er mod 1.5's libsrv_um (ABI-mismatch). pvrtld i
  dumpet er 32-bit og virkede at bytte, men ingen nytte når selve init'en fejler.
- [målt] **1.5's `libIMGegl.so` har samme minor-version-tjek som 1.4, men på 0xa180**
  (ikke 0x9194): `cmp r3,#1; bls.w` afviser minor>1 for major=3 (Firefox beder om
  minor=2). Patch (cmp r3,#255) er forberedt og md5-verificeret (077f5079), men nåede
  aldrig at blive testet — wedgen kom allerede ved EGL-init/connect.
- [målt] **Boksens eMMC/loop-image-risiko:** `/system` er et 192 MB ext2-loopimage
  (`/usr/local/share/libhybris/system.img`, mountet ro af myinit). Det var 100 % fuldt;
  en `cp` ind i det fulde image korrumperede ext2 (`ext2_free_blocks` med garbage-
  bloknumre) → hard lockup CPU 1 + tab af uflushed data ved power-cut (libc-filer blev
  nul-stillet). Image'et er nu forstørret til 254 MB (+64M, resize2fs) med 48 MB fri.
- [udført] **1.4-restore gennemført og verificeret:** alle 1.4-filer tilbage i
  /system fra `/root/ddk14-backup` (md5 matcher), 5.1-libc genskabt (0279adb4),
  bind-mount-patches genoprettet (bindapi + driver-minor), GPU-stak startet
  (pvrsrvctl-exit=0), baseline bekræftet: shader_ext_test = "Extension not supported"
  (1.4-adfærd).

## Status i ét blik

- **DDK 1.5-sporet er afsluttet med negativt resultat:** 1.5-UM (32-bit) + 6.0-libc
  hænger den indbyggede 1.4-æra-KM ("Rogue L 0.22") ved EGL-init — hard lockup, 2×
  bekræftet. KM'en kan ikke byttes uden kernel-rebuild (indbygget). Boksen er tilbage
  i kendt god 1.4-tilstand (WebGL/UI virker; Subway Surfers blokeres fortsat af
  GL_EXT_draw_buffers-kompileringsblokaden).
- **Nye spor-videre-muligheder (foreslået, IKKE aftalt):** (a) byg 1.5-KM ind i en
  kernel-rebuild (stort, kræver mmallow_kernel + aarch64-krydskompiler + flash),
  (b) find 1.5-KM som præbygget .ko der KAN loades (skal undersøges om kernen over-
  hovedet tillader modul-load: insmod gav "Invalid module format" på 1.4-ko'en — tyder
  på CONFIG_MODVERSIONS/vermagic-mismatch eller moduler slået fra), (c) find spil uden
  MRT-shaders, (d) acceptér begrænsningen.

## Detaljeret forløb

1. **Tilstandstjek:** /dev/pvrsrvkm findes; pvrsrvkm IKKE i /proc/modules; 1.4-filer
   md5 = boksens faktiske (afviger fra handoverens geekboxzone-reference for 6/7
   filer — backup af boksens egne filer er den eneste sande restore-vej).
2. **Backup:** `/root/ddk14-backup/` (egl/lib/hw/bin/patches + pvrsrvkm.ko + libc-5.1)
   — alle md5 verificeret efter genstarten.
3. **1.5-filer scp'ed til /root/ddk15/** (md5 matcher handoveren), minor-patch på
   libIMGegl 0xa180 forberedt.
4. **Installation:** `/system` var ro+fuldt → blockdev --setrw, remount rw, men cp
   fejlede (No space left) og truncat'ede egl-filer → ext2-korruption → hard lockup
   CPU 1 (03:17, kern.log: "Watchdog detected hard LOCKUP on cpu 1" efter
   `ext2_free_blocks: Freeing blocks not in datazone`). Reparation: unmount,
   billedbackup (/root/system.img.1.4.bak), e2fsck -fy, truncate +64M, resize2fs,
   remount, genskab 1.4-egl fra backup, kopiér 1.5 ind (OK, md5 verificeret).
5. **6.0-libc:** 1.5-libs'ene manglede `__register_atfork` (5.1-libc) → hentede
   6.0-libc fra dumpet, lagde ind med sync. pvrsrvctl 1.4 segfault'ede mod 1.5-
   libsrv_um; pvrsrvctl 1.5 (dump) er 64-bit og kan ikke exec'e.
6. **Første shader-test (03:2x):** hang → box nede (anden wedge). Denne gang var
   filsystemet RENT og 6.0-libc på plads → wedgen skyldes 1.5-UM mod indbygget KM
   (bridge-ABI), ikke filsystemet.
7. **Genstart + 1.4-restore:** alle filer tilbage fra backup, patches genoprettet,
   baseline bekræftet.

## Nøglekommandoer / beviser

```bash
# Wedge-bevis (kern.log, før genstart):
#   Watchdog detected hard LOCKUP on cpu 1   (03:17:00, efter ext2-korruption)
#   INFO: rcu_preempt detected stalls on CPUs/tasks: { 1}
#   (anden wedge: ingen ext2-fejl forud — ren 1.5-UM-test, hard lockup)

# KM indbygget:
dmesg | head -40          # [2.785] PVR_K: sys.gpvr.version=Rogue L 0.22
cat /proc/modules         # tom
grep -E "PVRSRV" /proc/kallsyms
rmmod pvrsrvkm            # Module unloading is not supported

# Backup / restore (på boksen):
#   backup:  /root/ddk14-backup/           (1.4, md5-verificeret)
#   restore: cp /root/ddk14-backup/{egl,lib,hw,bin}/* /system/... + libc-5.1
#   patches: bash devuan/gpu/eglplatform_x11/patch_{android_bindapi,driver_minor}.sh
#            (køres fra LAPTOP — scripts'ene har laptop-nøgle-stien)

# Image-forstørrelse (kun hvis nødvendigt igen):
#   umount /system; e2fsck -fy system.img; truncate -s +64M system.img;
#   resize2fs system.img; mount -o loop,ro ...

# Baseline (1.4, bekræftet efter restore):
#   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
#     EGL_PLATFORM=x11 DISPLAY=:0 /tmp/shader_ext_test
#   → ES2/ES3: compile=FEJL "Extension GL_EXT_draw_buffers not supported"
```

## Repo-tilstand / næste skridt

- Nye filer: dette notat; beviser: kern.log-uddrag i notatet; backup-kommandoer i
  DDK-HANDOVER (opdateret).
- TODO.md: DDK-sporet skal markeres "afprøvet, ikke anvendelig uden kernel-rebuild".
- Næste (foreslået): beslut om spor-videre (kernel-rebuild vs. spil uden MRT vs.
  accept), opdater DOKUMENTATION.md §5.15e + TODO.md.
