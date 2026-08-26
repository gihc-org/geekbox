# Kernel-rebuild til DDK 1.5 (3.10.79, lollipop_kernel geekbox)

## Status (26. aug 2026)

Planens præmis om at `mmallow_kernel`/`lollipop_kernel` indeholder 1.5-KM-kilde er
målt FORKERT — begge har `drivers/gpu/rogue` = 1.4@3632228. 1.5-KM findes kun som
præbygget `.ko` (leddaz-dump + mmallow_vendor G6110_64, identiske filer):
`/tmp/ddk15_km/pvrsrvkm_leddaz.ko`, vermagic `3.10.0 SMP preempt mod_unload aarch64`,
**uden modversions** → kan loades på en genbygget kerne uden indbygget PVR-KM.

Korrigeret vej (afventer brugerens go): baseline-kernel (uændret defconfig) →
testkernel med `CONFIG_POWERVR_ROGUE` FRA → SD/root-boot → `insmod` 1.5-.ko →
1.5-UM. "Indbygget fra kilde" kræver en 4.4→3.10-port (ayufan rock64-kernen har
1.5-kilde, men på 4.4.83) — dagevis arbejde.

## Byg

```bash
sudo apt-get install -y gcc-9-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  u-boot-tools device-tree-compiler bison flex
mkdir -p /tmp/kbcc && ln -s /usr/bin/aarch64-linux-gnu-gcc-9 /tmp/kbcc/aarch64-linux-gnu-gcc
bash devuan/gpu/kernelbuild/build_kernel.sh baseline
bash devuan/gpu/kernelbuild/package_bootimg.py \
  devuan/gpu/kernelbuild/out/baseline/Image \
  extracted/Image/ramfs.img \
  devuan/gpu/kernelbuild/out/baseline/ramfs-baseline.img
```

Kun kernel-arealet skiftes; ramdisk (initramfs) og second (rk-kernel.dtb + logo)
er de ORIGINALE — dvs. ingen DTB-ændring ved testen.

## Boot-/flash-noter

- RK3368 BootROM booter eMMC først → kernen kommer altid fra eMMC's boot-partition.
- "Boot fra SD" i dette projekt = root på SD (`DI -p` med `root=LABEL=sdrootfs1`
  eller `root=/dev/mmcblk1p1`), kernel stadig fra eMMC.
- Flash af ny kernel: `upgrade_tool DI -b <ramfs>.img` (boot-partitionen, 32 MB).
  Originalen gemt i `extracted/Image/ramfs.img` → rollback = `DI -b` med originalen.
- MiniLoader understøtter også fuld SD-boot (ID-block @ sector 64, FW @ 8192) —
  eksperiment (ufarligt, kun SD-skrivning), hvis vi vil undgå eMMC-flash.

## Fælder løst i build-scriptet

- `scripts/gcc-wrapper.py` (python2 + -Werror-politik) fjernes — bygger med gcc-9.
- dtc shipped-filer: `YYLTYPE yylloc` duplikat-definition → lexer patchet til extern.
- kconfig genererede filer findes som `.c_shipped` → ingen bison/flex-regenerering.
