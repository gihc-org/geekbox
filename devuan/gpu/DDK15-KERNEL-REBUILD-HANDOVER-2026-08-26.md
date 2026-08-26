# DDK 1.5 kernel-rebuild — handover 26. aug 2026 (~13:0x)

> Læs dette + `DDK15-KERNELREBUILD-SESSION-NOTAT-2026-08-26.md` (beslutninger,
> målinger) + `DDK15-KERNEL-REBUILD-LØSNING-2026-08-26.md` (blokade-løsninger) og
> fortsæt derfra. Mål: få 1.5-userspace til at køre på boks 1 (192.168.0.188) uden
> ABI-wedge — slutverifikation: `shader_ext_test` accepterer `GL_EXT_draw_buffers`
> og Firefox about:support viser "OpenGL ES 3.1 build 1.5@3830101".

## Status i ét blik

- **Rodårsagen til alle lilla-frysere er fundet og løst (26. aug ~12:5x):**
  manglende SHA1-`id`-felt (offset 0x240) i vores selv-pakkede bootimg. U-Boot
  verificerer det (`SecureVerify.c`) og afviser billedet når det er nul. Kernerne
  var sandsynligvis fine hele tiden — alle tre tidligere byg (gcc-9/gcc-8,
  jan-/juni-træ) frøs kun pga. pakningen.
- **Pakkeren er patchet:** `devuan/gpu/kernelbuild/package_bootimg.py` beregner
  id'et. Kontrol: original-kernen om-pakket → byte-identisk id `a40f20c6…`
  (= originalen), verificeret.
- **To billeder er bygget og klar til flash** (begge < 32 MB boot-partition):
  baseline (1.4-KM indbygget) og test (POWERVR_ROGUE slået fra, modul-støtte på —
  til 1.5-.ko'en). Se "Artefakter" nedenfor.
- **Boks-status ved sessionslut:** sidste melding var "lilla-frys" efter tidligere
  test. Kendt-god-tilstand = original kernel #168 (27. jan 2016) + eMMC-root;
  rollback-sæt er klar (`out/backup/` + `/root/kernel-test-backup/` på boksen).
  SD-kortet er klargjort med fuld rootfs (5,1 GB, "SD-KLAR") men kræves ikke til
  kernel-testen — kernen kommer kun fra eMMC's boot-partition.

## Nøglefakta (målt 26. aug)

- **1.5-KM findes IKKE som kilde til 3.10.** `geekboxzone/mmallow_kernel`
  (3.10.92) og `lollipop_kernel` (3.10.79), gren `geekbox`, har begge
  `drivers/gpu/rogue` = **1.4@3632228**. 1.5@3830101 findes kun som præbygget
  **.ko** (806.864 B, identiske filer i leddaz-dumpet og
  `mmallow_vendor_rockchip_common` G6110_64): `/tmp/ddk15_km/pvrsrvkm_leddaz.ko`
  (+ `_geekbox64.ko`). Vermagic `3.10.0 SMP preempt mod_unload aarch64`, **uden**
  `__versions` (CONFIG_MODVERSIONS off) → kan insmod'es på en genbygget kerne.
  Kilde til 1.5 findes kun i ayufan rock64-kernen (4.4.83) — port til 3.10 er
  dagevis arbejde, derfor .ko-vejen.
- **Id-algoritmen:** `id = SHA1(kernel ‖ u32le(kernel_size) ‖ ramdisk ‖
  u32le(ramdisk_size) ‖ second ‖ u32le(second_size) ‖ u32le(tags_addr) ‖
  u32le(page_size) ‖ unused[8] ‖ name[16] ‖ cmdline[512])`.
  Kilde: `/tmp/lollipop_uboot/board/rockchip/common/SecureBoot/SecureVerify.c:155-181`.
- **Byg-værktøj (virker):** `/tmp/lollipop_kernel` (jan-commit `80f6d15b9d2`),
  defconfig `/tmp/geekbox_defconfig_mar2016.txt`, gcc-9 via `/tmp/kbcc`
  (gcc-8.3 via `/tmp/kbcc8` fungerer også). Script:
  `devuan/gpu/kernelbuild/build_kernel.sh` (`MODE=baseline|test`).

## Artefakter

| Fil (på laptoppen) | Indhold | Id | Størrelse |
|---|---|---|---|
| `out/baseline/ramfs-baseline-id.img` | Baseline (PVR=y, 1.4 indbygget) | `5598c599…` | 29,7 MB |
| `out/test/ramfs-test-id.img` | Testkernel (PVR fra, moduler y) | `1aa2a7a2…` | 29,3 MB |
| `out/control/ramfs-control-new.img` | Original-kernel, vores pakning | `a40f20c6…` (= original) | 28,8 MB |
| `out/backup/ramfs_current.img` | Rollback: original boot-partition (32 MB dump) | — | 33,6 MB |
| `out/backup/param_current.bin` | Rollback: original parameter | — | 32 KB |

Kilder/reference: `extracted/Image/ramfs.img` (original bootimg),
`extracted/kernel` (original kernel #168). Parametre (CRLF!):
`parameter_emmc_myinit_cma.txt` (root=/dev/mmcblk0p6, init=/root/myinit.sh,
cma=128M) og `parameter_sdroot_myinit_cma.txt` (root=/dev/mmcblk1p1).

## Flash → verificér (næste skridt)

```bash
cd /home/kristian/projects/geekbox/Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23
# Boks 1 i loader-tilstand (Mask ROM), USB til laptoppen:
sudo ./upgrade_tool DI -p /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/parameter_emmc_myinit_cma.txt
sudo ./upgrade_tool DI -b /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/out/baseline/ramfs-baseline-id.img

# Boot-verifikation (LED skal blive blå; ellers lilla = fejl):
bash devuan/find_box.sh
ssh -i ~/.ssh/geekbox_key root@192.168.0.188 'uname -a'
ssh -i ~/.ssh/geekbox_key root@192.168.0.188 'timeout 60 env LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 /tmp/shader_ext_test'
#   Forventet nu: "Extension GL_EXT_draw_buffers not supported" (stadig 1.4)
```

Når baseline booter → testkernel + 1.5-KM:

```bash
sudo ./upgrade_tool DI -b /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/out/test/ramfs-test-id.img
scp -i ~/.ssh/geekbox_key /tmp/ddk15_km/pvrsrvkm_leddaz.ko root@192.168.0.188:/tmp/
ssh -i ~/.ssh/geekbox_key root@192.168.0.188 'insmod /tmp/pvrsrvkm_leddaz.ko; dmesg | tail -20'
#   Forventet i dmesg: "Rogue_DDK_Android rogueddk 1.5@3830101"
```

Rollback hvis noget fryser:

```bash
sudo ./upgrade_tool DI -b /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/out/backup/ramfs_current.img
sudo ./upgrade_tool DI -p /home/kristian/projects/geekbox/devuan/gpu/kernelbuild/parameter_emmc_myinit_cma.txt
```

## Næste skridt (rækkefølge)

1. Flash `ramfs-baseline-id.img` → verificér boot (bevis: uname + shader_ext_test).
2. Flash `ramfs-test-id.img` → `insmod` 1.5-.ko → dmesg `1.5@3830101`.
3. Læg 1.5-UM (32-bit-sæt fra leddaz-dumpet) i /system + løs de to blokader:
   `__register_atfork` (shim/patch, erstat IKKE hele /system-libc) og 64-bit
   `pvrsrvctl` (dumpets 64-bit-runtime: linker64 + lib64-sæt).
4. Verificér: `shader_ext_test` accepterer draw_buffers → `trivial_test`
   (draw_buffers OK) → Firefox about:support "OpenGL ES 3.1 build 1.5@3830101".
5. Fase B (når 1.5-vejen virker): merge til 3.10.108, jf. DRIVER-PORTERING.md §6.

## Fælder

- Lilla LED ved boot: tag dmesg/bevis straks; pak altid med `package_bootimg.py`
  (id-feltet). Parametre skal være CRLF (de er det i repoet).
- Boksen skal være i loader-tilstand før `upgrade_tool` virker; efter flash →
  strømcyklus (ikke `reboot` — det slukker boksen).
- SD-kortet kan sidde i boksen uden at påvirke kernel-testen (kernel kommer fra
  eMMC-boot-partitionen; parameteren peger på eMMC-root).
- `/proc/modules` er tomt på original-kernen (KM indbygget) — det er forventet, se
  DOKUMENTATION.md. Efter testkernel er moduler aktive.
- `pkill -9 -x firefox-esr` (aldrig `-f`), tjek /system-plads før kopiering.
- Korrigeret hovedvej (PVR fra + .ko) stod som [foreslået] i session-notatet —
  brugeren har fulgt den i praksis; formalisér gerne som [aftalt] i ny session.

## God start i en ny session

```text
Læs devuan/gpu/DDK15-KERNEL-REBUILD-HANDOVER-2026-08-26.md,
devuan/gpu/DDK15-KERNELREBUILD-SESSION-NOTAT-2026-08-26.md og
devuan/gpu/DDK15-KERNEL-REBUILD-LØSNING-2026-08-26.md og fortsæt derfra.
Status: bootimg-blokaden (SHA1-id) er løst; baseline- og testkernel er bygget og
pakket med korrekt id i devuan/gpu/kernelbuild/out/. Næste skridt: flash baseline
med parameter_emmc_myinit_cma.txt og verificér boot (LED blå, uname, shader_ext_test
= "not supported"); derefter flash testkernel og insmod /tmp/ddk15_km/pvrsrvkm_leddaz.ko
(dmesg skal vise Rogue_DDK_Android rogueddk 1.5@3830101). Boks 1 = 192.168.0.188,
ssh med ~/.ssh/geekbox_key, find med devuan/find_box.sh. Rollback: out/backup/ramfs_current.img
+ parameter_emmc_myinit_cma.txt. Fælder: pak altid med package_bootimg.py (id-feltet),
CRLF-parametre, strømcyklus efter flash, pkill -9 -x firefox-esr (aldrig -f).
Gør derefter 1.5-UM-vejen færdig: __register_atfork-shim + 64-bit-pvrsrvctl, og
verificér draw_buffers + Firefox about:support "OpenGL ES 3.1 build 1.5@3830101".
```
