#!/usr/bin/env python3
"""fb_overscan.py — kompenser for TV'ets overscan ved at skrumpe billedet.

PROBLEMET: TV'et (4K Samsung) beskærer cirka 2,3 % af billedet på alle fire kanter —
~25 linjer i top og bund, ~48 pixels i hver side. Målt 18. aug 2026 med farvebjælker i
kendte højder: den hvide 4 px ramme og bjælker 10 px inde var usynlige i begge ender,
mens bjælker 30 px inde var synlige; menu-ikonet på x=0-40 var også usynligt.
Konsekvensen er at lxpanel (26 px højt, nederst) forsvinder — symptomet "sort skærm med
kun en musemarkør" efter flash, når skrivebordsbaggrunden samtidig mangler.

HVORFOR DENNE VEJ: vendor-kernens egen overscan-kompensation er død kode —
`rk_fb_disp_scale()` i drivers/video/rockchip/rk_fb.c returnerer straks når HDMI er
primær skærm (altså altid på denne boks), så `/sys/class/display/HDMI/scale` kan skrives
uden nogen effekt. Men `rk_fb_set_par()` (samme fil) læser vinduets størrelse og position
ud af fb-var'ens `grayscale` og `nonstd`:

    xsize = (var->grayscale >>  8) & 0xfff     ysize = (var->grayscale >> 20) & 0xfff
    xpos  = (var->nonstd    >>  8) & 0xfff     ypos  = (var->nonstd    >> 20) & 0xfff
    data_format = var->nonstd & 0xff           <-- lav byte SKAL bevares (er 4 her)

Sætter man dem via FBIOPUT_VSCREENINFO, programmerer driveren win0 til at vise
framebufferen nedskaleret i et centreret vindue (VOP'ens egen scaler, y_h_fac/y_v_fac).
X tegner fortsat 1920x1080 og ved intet om det; musen passer stadig 1:1.

Verificeret på boks 3: dsp_x/dsp_y 1824x1026, x_st/y_st 48/27, faktorer 4310/4309.

SKAL KØRE EFTER X ER STARTET — X's eget set_par ved opstart nulstiller grayskale/nonstd.
Hænges derfor op i /etc/xdg/lxsession/LXDE/autostart (uden @, den skal kun køre én gang).

Brug:
  fb_overscan.py                  # 95 % (standard)
  fb_overscan.py --percent 97     # mindre kompensation, mere billede
  fb_overscan.py --reset          # tilbage til fuld skærm
  fb_overscan.py --show           # vis nuværende tilstand, rør intet
"""
import argparse
import fcntl
import os
import struct
import sys

FBIOGET_VSCREENINFO = 0x4600
FBIOPUT_VSCREENINFO = 0x4601
# fb_var_screeninfo = 40 x __u32 (ingen pointere, så samme layout i 32- og 64-bit)
VAR_FMT = "<40I"
VAR_SIZE = struct.calcsize(VAR_FMT)
I_XRES, I_YRES, I_GRAYSCALE, I_NONSTD = 0, 1, 7, 20


def laes_var(fd):
    buf = bytearray(VAR_SIZE)
    fcntl.ioctl(fd, FBIOGET_VSCREENINFO, buf)
    return list(struct.unpack(VAR_FMT, buf))


def vis(v, hvad):
    xs, ys = (v[I_GRAYSCALE] >> 8) & 0xFFF, (v[I_GRAYSCALE] >> 20) & 0xFFF
    xp, yp = (v[I_NONSTD] >> 8) & 0xFFF, (v[I_NONSTD] >> 20) & 0xFFF
    if xs == 0 and ys == 0:
        print(f"{hvad}: fuld skærm ({v[I_XRES]}x{v[I_YRES]}), ingen kompensation")
    else:
        print(f"{hvad}: vindue {xs}x{ys} på position {xp},{yp} "
              f"(skærm {v[I_XRES]}x{v[I_YRES]})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--percent", type=int, default=95,
                    help="hvor stor en del af skærmen billedet skal fylde (50-100, "
                         "standard 95)")
    ap.add_argument("--reset", action="store_true", help="tilbage til fuld skærm")
    ap.add_argument("--show", action="store_true", help="vis tilstand, rør intet")
    ap.add_argument("--fb", default="/dev/fb0")
    args = ap.parse_args()

    if not 50 <= args.percent <= 100:
        sys.exit("FEJL: --percent skal være mellem 50 og 100")

    try:
        fd = os.open(args.fb, os.O_RDWR)
    except OSError as e:
        sys.exit(f"FEJL: kan ikke åbne {args.fb}: {e}")

    v = laes_var(fd)
    vis(v, "før")
    if args.show:
        return

    if args.reset:
        xs = ys = xp = yp = 0
    else:
        xs = v[I_XRES] * args.percent // 100
        ys = v[I_YRES] * args.percent // 100
        # VOP'en kræver lige tal på bredden (RGB565, 4-byte-ord); hold begge lige
        xs -= xs % 2
        ys -= ys % 2
        xp = (v[I_XRES] - xs) // 2
        yp = (v[I_YRES] - ys) // 2
        for navn, tal in (("xsize", xs), ("ysize", ys), ("xpos", xp), ("ypos", yp)):
            if tal > 0xFFF:
                sys.exit(f"FEJL: {navn}={tal} kan ikke rummes i 12 bit")

    v[I_GRAYSCALE] = (xs << 8) | (ys << 20)
    v[I_NONSTD] = (v[I_NONSTD] & 0xFF) | (xp << 8) | (yp << 20)  # bevar data_format
    try:
        fcntl.ioctl(fd, FBIOPUT_VSCREENINFO, struct.pack(VAR_FMT, *v))
    except OSError as e:
        sys.exit(f"FEJL: FBIOPUT_VSCREENINFO: {e}")

    vis(laes_var(fd), "efter")


if __name__ == "__main__":
    main()
