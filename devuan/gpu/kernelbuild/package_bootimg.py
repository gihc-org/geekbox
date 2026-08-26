#!/usr/bin/env python3
"""Pakker en ny kernel (Image) ind i en Android-bootimg med den ORIGINALE
ramdisk + second (DTB) fra extracted/Image/ramfs.img.

Brug: package_bootimg.py <ny-Image> <original-ramfs.img> <output-ramfs.img>
"""
import struct, sys

def align(x, page):
    return (x + page - 1) // page * page

def main():
    newimg, orig, out = sys.argv[1:4]
    kernel = open(newimg, "rb").read()
    origd = open(orig, "rb").read()
    hdr = origd[:512]
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

    newks = len(kernel)
    newrds = len(ramdisk)
    newss = len(second)
    new_roff = koff + align(newks, ps)
    new_soff = new_roff + align(newrds, ps)
    total = new_soff + align(newss, ps)

    newhdr = bytearray(hdr)
    struct.pack_into("<8I", newhdr, 8, newks, ka, newrds, rda, newss, sa, ta, ps)
    buf = bytearray(total)
    buf[0:512] = newhdr
    buf[koff:koff + newks] = kernel
    buf[new_roff:new_roff + newrds] = ramdisk
    buf[new_soff:new_soff + newss] = second
    open(out, "wb").write(bytes(buf))
    print(f"OK: {out} ({total} bytes, kernel {newks}, ramdisk {newrds}, second {newss})")

if __name__ == "__main__":
    main()
