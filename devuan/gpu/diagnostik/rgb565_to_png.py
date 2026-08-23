#!/usr/bin/env python3
"""rgb565_to_png.py — konverter rå RGB565-framebuffer-dump til PNG.

Brug:  python3 rgb565_to_png.py <input.raw> <output.png> [bredde] [højde]
Standard: 1920x1080, little-endian RGB565, stride = bredde*2 (boksen: /dev/fb0).
"""
import struct
import sys
import zlib


def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def convert(src, dst, w=1920, h=1080):
    data = open(src, "rb").read()
    expected = w * h * 2
    if len(data) < expected:
        raise SystemExit(f"FEJL: dump er {len(data)} bytes, forventede {expected}")
    raw = bytearray()
    for y in range(h):
        row = data[y * w * 2:(y + 1) * w * 2]
        for x in range(w):
            v = struct.unpack_from("<H", row, x * 2)[0]
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            raw += bytes((r, g, b))
    scanlines = bytearray()
    for y in range(h):
        scanlines += b"\x00" + raw[y * w * 3:(y + 1) * w * 3]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(scanlines), 6)) + chunk(b"IEND", b""))
    open(dst, "wb").write(png)
    print(f"{src} -> {dst} ({w}x{h}, {len(png)} bytes PNG)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 1920
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 1080
    convert(sys.argv[1], sys.argv[2], w, h)
