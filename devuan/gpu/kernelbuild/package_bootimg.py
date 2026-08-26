#!/usr/bin/env python3
"""Pakker en ny kernel (Image) ind i en Android-bootimg med den ORIGINALE
ramdisk + second (DTB) fra extracted/Image/ramfs.img.

Id-feltet (offset 0x240, 20 bytes) beregnes som Rockchip-U-Boot gør i
SecureVerify.c (SecureNSModeBootImageShaCheck):

    SHA1(kernel || u32(kernel_size) || ramdisk || u32(ramdisk_size)
        || second || u32(second_size) || u32(tags_addr) || u32(page_size)
        || unused[8] || name[16] || cmdline[512])

Uden dette id afviser boksen billedet (lilla LED / fryser ved logo).

Brug: package_bootimg.py <ny-Image> <original-ramfs.img> <output-ramfs.img>
"""
import hashlib, struct, sys

def align(x, page):
    return (x + page - 1) // page * page

def compute_id(kernel, ks, ramdisk, rds, second, ss, ta, ps,
               unused, name, cmdline):
    """Rockchip SHA1-id: kernel+ramdisk+second med stoerrelser og
    resten af headeren (SecureVerify.c-kompatibel)."""
    h = hashlib.sha1()
    for part in (kernel, struct.pack("<I", ks), ramdisk, struct.pack("<I", rds),
                 second, struct.pack("<I", ss), struct.pack("<I", ta),
                 struct.pack("<I", ps), unused, name, cmdline):
        h.update(part)
    return h.digest()

def main():
    newimg, orig, out = sys.argv[1:4]
    kernel = open(newimg, "rb").read()
    origd = open(orig, "rb").read()
    hdr = origd[:2048]
    magic = hdr[:8]
    assert magic == b"ANDROID!", "ikke en Android-bootimg"
    (ks, ka, rds, rda, ss, sa, ta, ps) = struct.unpack_from("<8I", hdr, 8)
    assert ps in (512, 2048, 4096, 8192, 16384), f"uventet page_size {ps}"

    hoff = align(512, ps)
    koff = hoff
    roff = koff + align(ks, ps)
    soff = roff + align(rds, ps)
    ramdisk = origd[roff:roff + rds]
    second = origd[soff:soff + ss]
    assert len(ramdisk) == rds and len(second) == ss, "ramdisk/second ekstrahering fejlede"

    # Header-felter der indgaar i SHA1 (uændret fra originalen):
    unused = hdr[40:48]
    name = hdr[48:64]
    cmdline = hdr[64:576]

    newks = len(kernel)
    newrds = len(ramdisk)
    newss = len(second)
    new_roff = koff + align(newks, ps)
    new_soff = new_roff + align(newrds, ps)
    total = new_soff + align(newss, ps)

    newhdr = bytearray(hdr)
    struct.pack_into("<8I", newhdr, 8, newks, ka, newrds, rda, newss, sa, ta, ps)
    newid = compute_id(kernel, newks, ramdisk, newrds, second, newss,
                       ta, ps, unused, name, cmdline)
    assert len(newid) == 20
    newhdr[576:596] = newid
    buf = bytearray(total)
    buf[0:2048] = newhdr
    buf[koff:koff + newks] = kernel
    buf[new_roff:new_roff + newrds] = ramdisk
    buf[new_soff:new_soff + newss] = second
    open(out, "wb").write(bytes(buf))
    print(f"OK: {out} ({total} bytes, kernel {newks}, ramdisk {newrds}, "
          f"second {newss}) id={newid.hex()}")

if __name__ == "__main__":
    main()
