#!/usr/bin/env python3
# Bygger en binær PARM-parameterfil (med Rockchips crc32_rk) ud af en tekst-parameter.
# Brug: ./make_parm_bin.py parameter.txt parameter.bin
import re, sys

tbl_src = open('lollipop_u-boot/lib/crc32_rk.c').read()
TBL = [int(m, 16) for m in re.findall(r'tole\(0x([0-9a-fA-F]+)L\)', tbl_src)]

def crc32_rk(data, crc=0):
    for b in data:
        crc = (TBL[((crc >> 24) ^ b) & 0xff] ^ ((crc << 8) & 0xffffffff)) & 0xffffffff
    return crc

txt = open(sys.argv[1], 'rb').read()
out = b'PARM' + len(txt).to_bytes(4, 'little') + txt + crc32_rk(txt).to_bytes(4, 'little')
open(sys.argv[2], 'wb').write(out)
assert crc32_rk(txt) == int.from_bytes(out[-4:], 'little')
print(f"{sys.argv[2]}: {len(out)} bytes, CRC ok")
