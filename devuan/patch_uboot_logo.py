#!/usr/bin/env python3
"""patch_uboot_logo.py — sætter DT-flaget /fb/rockchip,uboot-logo-on i et Rockchip-image.

HVORFOR (se DEBUG-SORT-SKAERM.md): med `uboot-logo-on = <1>` overtager vendor-kernen
U-Boots display-opsætning i stedet for at programmere LCDC'en selv:

  drivers/video/rockchip/lcdc/rk3368_lcdc.c:2239
      if (support_uboot_display() && lcdc_dev->prop == PRMRY)
              rk3368_lcdc_set_dclk(dev_drv, 0);       <-- KUN dclk
      else
              rk3368_load_screen(dev_drv, 1);         <-- fuld timing-programmering

  drivers/video/rockchip/rk_fb.c:3559 (ved HDMI-connect)
      if (!dev_drv->uboot_logo || load_screen ||
          (policy != BOX && policy != BOX_TEMP))      <-- vores DT: policy=2 (BOX_TEMP)
              ... load_screen() ...
  hvor `load_screen` kun sættes hvis HDMI'ens opløsning AFVIGER fra den, kernen
  læste ud af U-Boots egne registre (rk3368_lcdc.c:401-415).

  Flaget nulstilles kun i RK_FBIOSET_CONFIG_DONE-ioctl'en (rk_fb.c:2856) — den
  kalder Androids display-stak, ikke X/fbdev. På vores boks er `uboot_logo`
  altså 1 for evigt, og kernen programmerer ALDRIG skærm-timingen.

Med flaget på 0 programmerer kernen selv timingen ved hver boot (deterministisk),
og HDMI-connect/DPMS-opvågnen går gennem load_screen som den skal.

Filen patches IN-PLACE og størrelsen er uændret (ét FDT-ord skifter værdi), så et
patchet image kan dd'es direkte oven i det oprindelige — headere/offsets rører sig ikke.
FDT har ingen checksum, og hverken U-Boot eller loaderen verificerer bootimg'ens
SHA1 (board/rockchip/common/rkloader/rkimage.c:654 læser blot second-arealet).

Understøtter fire filtyper og finder selv DTB'en:
  * RKFW update.img ("RKFW")      — patcher BEGGE DTB-kopier: Image/ramfs.img og
    Image/resource.img.
  * Android bootimg ("ANDROID!")  — DTB'en ligger i second-arealet (= et RSCE-image).
    Det er DENNE kopi U-Boot faktisk bruger: rkimage_prepare_fdt() henter fdt fra
    BOOT-partitionen først og falder først derefter tilbage til resource-partitionen.
  * RSCE-resource-image ("RSCE")  — fx Image/resource.img (fallback-kopien).
  * Rå DTB ("\\xd0\\x0d\\xfe\\xed").

Brug:
  devuan/patch_uboot_logo.py devuan/update_devuan.img         # sæt til 0 (begge kopier)
  devuan/patch_uboot_logo.py <fil> --value 1                  # tilbage til vendor-adfærd
  devuan/patch_uboot_logo.py <fil> --check                    # læs kun, rør ikke filen
"""
import argparse
import struct
import sys

PROP = "rockchip,uboot-logo-on"
NODE = "/fb"
FDT_MAGIC = b"\xd0\x0d\xfe\xed"


def align(val, n):
    return (val + n - 1) // n * n


def rkfw_entries(data, wanted):
    """Offsets til de ønskede entry'er i et RKFW-update.img."""
    if data[0:4] != b"RKFW":
        return None
    upd_off = struct.unpack_from("<I", data, 0x21)[0]
    n = struct.unpack_from("<I", data, upd_off + 0x88)[0]
    out = {}
    for i in range(n):
        ent = upd_off + 0x8C + i * 0x70
        path = data[ent + 0x20:ent + 0x40].split(b"\0")[0].decode("latin1")
        if path in wanted:
            out[path] = upd_off + struct.unpack_from("<I", data, ent + 0x60)[0]
    missing = [w for w in wanted if w not in out]
    if missing:
        sys.exit(f"FEJL: fandt ikke {missing} i RKFW-imaget")
    return out


def find_dtb(data, base=0, what="fil"):
    """Returnér absolut offset til DTB'en i data[base:] — uanset indpakning."""
    magic = data[base:base + 8]
    if magic[:4] == FDT_MAGIC:
        return base
    if magic == b"ANDROID!":
        kernel_size, _, ramdisk_size, _, second_size, _, _, page_size = \
            struct.unpack_from("<8I", data, base + 8)
        if not second_size:
            sys.exit(f"FEJL: bootimg ({what}) har intet second-areal — ingen DTB")
        off = base + page_size + align(kernel_size, page_size) + \
            align(ramdisk_size, page_size)
        return find_dtb(data, off, what="bootimg-second")
    if magic[:4] == b"RSCE":
        n_files = struct.unpack_from("<I", data, base + 0xC)[0]
        for i in range(1, n_files + 1):
            ent = base + i * 512
            if data[ent:ent + 4] != b"ENTR":
                continue
            name = data[ent + 4:ent + 4 + 220].split(b"\0")[0].decode("latin1")
            blk = struct.unpack_from("<I", data, ent + 0x104)[0]
            if name.endswith(".dtb"):
                return find_dtb(data, base + blk * 512, what=f"RSCE:{name}")
        sys.exit(f"FEJL: ingen .dtb i RSCE-indekset ({what})")
    sys.exit(f"FEJL: ukendt filtype ({what}): magic={magic!r}")


def fdt_find_prop(data, dtb, node_path, prop_name):
    """Absolut offset til værdien af prop_name i node_path. Minimal FDT-walker."""
    (magic, _totalsize, off_struct, off_strings, _off_rsvmap, _ver, _last,
     _cpu, _size_strings, size_struct) = struct.unpack_from(">10I", data, dtb)
    if magic != 0xD00DFEED:
        sys.exit("FEJL: DTB-magic mangler")

    def string_at(off):
        start = dtb + off_strings + off
        return data[start:data.index(b"\0", start)].decode("latin1")

    p = dtb + off_struct
    end = p + size_struct
    path = []
    while p < end:
        tok = struct.unpack_from(">I", data, p)[0]
        p += 4
        if tok == 1:                                    # FDT_BEGIN_NODE
            e = data.index(b"\0", p)
            path.append(data[p:e].decode("latin1"))
            p = align(e + 1 - dtb, 4) + dtb
        elif tok == 2:                                  # FDT_END_NODE
            path.pop()
        elif tok == 3:                                  # FDT_PROP
            length, nameoff = struct.unpack_from(">II", data, p)
            p += 8
            here = "/" + "/".join(path[1:])
            if here == node_path and string_at(nameoff) == prop_name:
                if length != 4:
                    sys.exit(f"FEJL: {prop_name} er {length} bytes, ventede 4")
                return p
            p = align(p + length - dtb, 4) + dtb
        elif tok == 4:                                  # FDT_NOP
            pass
        elif tok == 9:                                  # FDT_END
            break
        else:
            sys.exit(f"FEJL: ukendt FDT-token {tok}")
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fil", help="bootimg, RSCE-resource-image eller rå DTB")
    ap.add_argument("--value", type=int, default=0,
                    help="ny værdi (standard: 0). u32, big-endian som FDT kræver")
    ap.add_argument("--prop", default=PROP,
                    help="property i /fb-noden (standard: %s). Alternativet er "
                         "rockchip,disp-policy: 2=BOX_TEMP (vendor), 0=SDK — sidstnævnte "
                         "får kernen til at kalde load_screen() ved HDMI-connect UDEN at "
                         "røre loaderens logo-sti" % PROP)
    ap.add_argument("--check", action="store_true", help="læs og rapportér, skriv ikke")
    args = ap.parse_args()
    prop = args.prop

    with open(args.fil, "rb") as f:
        data = bytearray(f.read())

    entries = rkfw_entries(data, ("Image/ramfs.img", "Image/resource.img"))
    if entries:
        targets = [(name, find_dtb(data, base, what=name))
                   for name, base in entries.items()]
    else:
        targets = [(args.fil, find_dtb(data))]

    writes = []
    for name, dtb in targets:
        off = fdt_find_prop(data, dtb, NODE, prop)
        if off is None:
            sys.exit(f"FEJL: {NODE}/{prop} findes ikke i DTB'en @ {dtb} ({name}) — "
                     "forkert image, eller vendor har omdøbt property'en")
        old = struct.unpack_from(">I", data, off)[0]
        print(f"{name}: DTB @ {dtb}, {NODE}/{prop} @ {off} = {old}")
        if args.check:
            continue
        if old == args.value:
            print(f"   uændret (allerede {args.value}) — intet skrevet")
            continue
        writes.append((off, old))

    if args.check or not writes:
        return

    with open(args.fil, "r+b") as f:
        for off, old in writes:
            f.seek(off)
            f.write(struct.pack(">I", args.value))
            print(f"   @ {off}: {old} -> {args.value} "
                  "(4 bytes, filstørrelse uændret)")

    # læs offsets igen fra disk som selvkontrol
    with open(args.fil, "rb") as f:
        for off, _old in writes:
            f.seek(off)
            now = struct.unpack(">I", f.read(4))[0]
            if now != args.value:
                sys.exit(f"FEJL: efterkontrol @ {off} læste {now}, "
                         f"ventede {args.value}")


if __name__ == "__main__":
    main()
