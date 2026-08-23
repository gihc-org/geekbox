#!/usr/bin/env python3
"""frontend.py — Python-frontend til GLES-daemonen (M2, aug 2026).

Tegner UI direkte paa /dev/fb0 (teknikken fra fb_overscan.py: ioctl + mmap) og
taler med gles_daemon over en unix-socket med JSON-linjer. Daemonen renderer
GLES-scenen offscreen og blitter KUN sin rect; frontenden tegner baggrund,
rammer og tekst rundt om og opdaterer status-teksten mellem hvert render.

Brug:
  python3 frontend.py                     # demo: 30 frames @ 5 fps, rect 960x540
  python3 frontend.py --frames 60 --fps 10
  python3 frontend.py --no-gles           # kun UI (ingen daemon noedvendig)
  python3 frontend.py --dump /tmp/fb.raw  # gem raa fb0 (stride*yres bytes) til sidst

Krav: X stoppet (service nodm stop), GPU-stak oppe (gpu_up.sh), gles_daemon
kørende. Kun Python 3-standardbiblioteket.
"""
import argparse
import fcntl
import json
import mmap
import os
import socket
import struct
import sys
import time

FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602
# fb_var_screeninfo paa 3.10-arm = 40 x u32 (samme som fb_overscan.py)
VAR_FMT = "<40I"
VAR_SIZE = struct.calcsize(VAR_FMT)
# fb_fix_screeninfo: vi læser kun to felter, med eksplicitte byte-offsets.
# NB: paa 32-bit ARM ligger line_length paa offset 44, IKKE 42 — den __u32
# skal 4-byte-alignes efter tre __u16'ere, saa kompilatoren (og kernen)
# indskyder 2 bytes padding. Maalt paa boksen aug 2026; C-structen klarer
# det selv, en "<40I"-lignende pakning gør det ikke.
FIX_SIZE = 68

I_XRES, I_YRES = 0, 1
I_XOFF, I_YOFF = 4, 5
I_BPP = 6
I_RED_OFF, I_RED_LEN = 8, 9
I_GREEN_OFF, I_GREEN_LEN = 11, 12
I_BLUE_OFF, I_BLUE_LEN = 14, 15


class Framebuffer:
    """fb0 med mmap + generisk pixel-pakning (16/24/32 bpp via fb_var-farver)."""

    def __init__(self, path="/dev/fb0"):
        self.fd = os.open(path, os.O_RDWR)
        var_buf = bytearray(VAR_SIZE)
        fcntl.ioctl(self.fd, FBIOGET_VSCREENINFO, var_buf)
        var = list(struct.unpack(VAR_FMT, var_buf))
        fix_raw = bytearray(FIX_SIZE)
        fcntl.ioctl(self.fd, FBIOGET_FSCREENINFO, fix_raw)
        smem_len = struct.unpack_from("<I", fix_raw, 20)[0]
        line_length = struct.unpack_from("<I", fix_raw, 44)[0]

        self.xres, self.yres = var[I_XRES], var[I_YRES]
        self.xoff, self.yoff = var[I_XOFF], var[I_YOFF]
        self.bpp = var[I_BPP]
        self.bytespp = self.bpp // 8
        self.stride = line_length
        expected = self.xres * self.bytespp
        if not (expected <= self.stride <= smem_len):
            print(f"frontend: advarsel — line_length {self.stride} ser forkert ud, "
                  f"bruger {expected}")
            self.stride = self.xres * self.bytespp
        self.r_off, self.r_len = var[I_RED_OFF], var[I_RED_LEN]
        self.g_off, self.g_len = var[I_GREEN_OFF], var[I_GREEN_LEN]
        self.b_off, self.b_len = var[I_BLUE_OFF], var[I_BLUE_LEN]
        if self.bytespp < 2:
            raise SystemExit(f"FEJL: underligt fb0-format: {self.xres}x{self.yres} bpp={self.bpp}")
        self.m = mmap.mmap(self.fd, self.stride * self.yres, mmap.MAP_SHARED)
        print(f"frontend: fb0 = {self.xres}x{self.yres} bpp={self.bpp} stride={self.stride}")

    @staticmethod
    def _ch(v, ln):
        if ln <= 0:
            return 0
        if ln >= 8:
            return v
        return (v >> (8 - ln)) & ((1 << ln) - 1)

    def pack(self, r, g, b):
        return ((self._ch(r, self.r_len) << self.r_off) |
                (self._ch(g, self.g_len) << self.g_off) |
                (self._ch(b, self.b_len) << self.b_off))

    def _off(self, x, y):
        return (y + self.yoff) * self.stride + (x + self.xoff) * self.bytespp

    def put_pixel(self, x, y, color):
        if not (0 <= x < self.xres and 0 <= y < self.yres):
            return
        p = self.pack(*color)
        off = self._off(x, y)
        if self.bytespp == 2:
            struct.pack_into("<H", self.m, off, p)
        elif self.bytespp == 4:
            struct.pack_into("<I", self.m, off, p)
        else:
            self.m[off:off + self.bytespp] = p.to_bytes(self.bytespp, "little")

    def fill_rect(self, x, y, w, h, color):
        x = max(0, min(x, self.xres))
        y = max(0, min(y, self.yres))
        x2 = max(x, min(x + w, self.xres))
        y2 = max(y, min(y + h, self.yres))
        w, h = x2 - x, y2 - y
        if w <= 0 or h <= 0:
            return
        row = bytearray(w * self.bytespp)
        p = self.pack(*color)
        if self.bytespp == 2:
            for i in range(w):
                struct.pack_into("<H", row, i * 2, p)
        elif self.bytespp == 4:
            for i in range(w):
                struct.pack_into("<I", row, i * 4, p)
        else:
            pb = p.to_bytes(self.bytespp, "little")
            for i in range(w):
                row[i * self.bytespp:(i + 1) * self.bytespp] = pb
        row = bytes(row)
        for yy in range(y, y2):
            off = self._off(x, yy)
            self.m[off:off + len(row)] = row

    def draw_text(self, x, y, text, color, scale=1):
        """5x7-font; ukendte/laengere tegn normaliseres (se FONT/NORMALIZE)."""
        cx = x
        for ch in text:
            for c in NORMALIZE.get(ch, ch):
                glyph = FONT.get(c, FONT.get(c.upper(), FONT[" "]))
                if glyph is FONT[" "]:
                    cx += 6 * scale
                    continue
                for ry, row in enumerate(glyph):
                    for rx, bit in enumerate(row):
                        if bit == "#":
                            self.put_pixel(cx + rx * scale, y + ry * scale, color)
                cx += 6 * scale
        return cx


class DaemonClient:
    """Unix-socket-klient: JSON-linjer ind, JSON-svar ud (synkront)."""

    def __init__(self, sock_path, timeout=10.0):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(timeout)
        self.s.connect(sock_path)

    def cmd(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        data = self.s.recv(65536)
        if not data:
            return {"ok": False, "error": "daemonen lukkede forbindelsen"}
        return json.loads(data.decode().strip())

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def parse_rect(spec):
    """'WxH+X+Y' -> (x, y, w, h) med x,y som øverste venstre hjørne."""
    try:
        dim, pos = spec.split("+", 1)
        w, h = (int(v) for v in dim.split("x"))
        x, y = (int(v) for v in pos.split("+"))
    except ValueError:
        raise SystemExit(f"FEJL: --rect skal være 'WxH+X+Y', fik '{spec}'")
    return x, y, w, h


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fb", default="/dev/fb0")
    ap.add_argument("--socket", default="/tmp/gles.sock")
    ap.add_argument("--rect", default="960x540+480+270",
                    help="GLES-rect som WxH+X+Y (daemonen blitter kun her)")
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--fps", type=float, default=5.0)
    ap.add_argument("--no-gles", action="store_true",
                    help="kun UI paa fb0 — ingen socket/daemon")
    ap.add_argument("--dump", metavar="RAW",
                    help="gem raa fb0 (stride*yres bytes) til denne fil til sidst")
    ap.add_argument("--title", default="GLES-DAEMON DEMO")
    args = ap.parse_args()

    fb = Framebuffer(args.fb)
    rx, ry, rw, rh = parse_rect(args.rect)
    if rw <= 0 or rh <= 0 or rx < 0 or ry < 0 or rx + rw > fb.xres or ry + rh > fb.yres:
        raise SystemExit(f"FEJL: rect {args.rect} ligger uden for skærmen {fb.xres}x{fb.yres}")

    BG = (18, 22, 30)
    FRAME = (120, 190, 255)
    TITLE = (235, 240, 245)
    SUB = (150, 160, 175)
    OK = (160, 255, 160)
    ERR = (255, 120, 100)
    WARN = (255, 190, 80)

    fb.fill_rect(0, 0, fb.xres, fb.yres, BG)
    # ramme om GLES-rect'en (4 px) — daemonen rører aldrig rammen
    fb.fill_rect(rx - 8, ry - 8, rw + 16, 4, FRAME)
    fb.fill_rect(rx - 8, ry + rh + 4, rw + 16, 4, FRAME)
    fb.fill_rect(rx - 8, ry - 8, 4, rh + 16, FRAME)
    fb.fill_rect(rx + rw + 4, ry - 8, 4, rh + 16, FRAME)

    fb.draw_text(24, 18, args.title, TITLE, scale=2)
    fb.draw_text(24, 44, "frontend.py — UI paa fb0, GLES via daemon", SUB)
    fb.draw_text(24, 62, f"rect {rw}x{rh}+{rx}+{ry}  fb {fb.xres}x{fb.yres} bpp={fb.bpp}", SUB)

    if args.no_gles:
        fb.draw_text(24, fb.yres - 50, "kun UI tegnet (--no-gles) — ingen daemon", WARN)
        print("frontend: UI tegnet uden GLES")
        if args.dump:
            _dump(fb, args.dump)
        return 0

    try:
        client = DaemonClient(args.socket)
    except OSError as e:
        fb.draw_text(24, fb.yres - 50, f"kan ikke forbinde til {args.socket}: {e}", ERR)
        print(f"frontend: FEJL — {e}")
        if args.dump:
            _dump(fb, args.dump)
        return 2

    fb_info = client.cmd({"cmd": "fb"})
    if fb_info.get("ok"):
        d = fb_info["fb"]
        fb.draw_text(24, 80,
                     f"daemon: GLES {fb_info.get('gl', '?')}  fb {d['xres']}x{d['yres']} bpp={d['bpp']}",
                     SUB)
    else:
        fb.draw_text(24, 80, "daemon: fb-svar fejlede", ERR)

    step = 6.283185307179586 / max(1, args.frames)
    t_next = time.monotonic()
    for i in range(args.frames):
        phase = i * step
        r = client.cmd({"cmd": "render", "scene": "triangle",
                        "rect": [rx, ry, rw, rh], "phase": round(phase, 3)})
        if not r.get("ok"):
            fb.fill_rect(24, fb.yres - 52, fb.xres - 48, 16, BG)
            fb.draw_text(24, fb.yres - 50,
                         f"render {i + 1} fejlede: {r.get('error', '?')}", ERR)
            print(f"frontend: render fejlede — {r.get('error', '?')}")
            break
        ms = r.get("ms", 0.0)
        fb.fill_rect(24, fb.yres - 52, fb.xres - 48, 16, BG)
        fb.draw_text(24, fb.yres - 50,
                     f"frame {i + 1}/{args.frames}  fase {phase:.2f}  render {ms:.0f} ms", OK)
        t_next += 1.0 / args.fps
        delay = t_next - time.monotonic()
        if delay > 0:
            time.sleep(delay)

    fb.fill_rect(24, fb.yres - 52, fb.xres - 48, 16, BG)
    fb.draw_text(24, fb.yres - 50, "færdig — sidste frame står på skærmen", TITLE)
    print("frontend: færdig")
    client.close()

    if args.dump:
        _dump(fb, args.dump)
    return 0


def _dump(fb, path):
    with open(path, "wb") as f:
        f.write(fb.m[:fb.stride * fb.yres])
    print(f"frontend: fb0 dumpet til {path} ({fb.stride * fb.yres} bytes)")


# 5x7-bitmapfont (basis: klassisk 5x7). '#' = pixel. a-z kortlægges til A-Z;
# æ/ø/å har egne små bogstaver; Æ/Ø/Å normaliseres (se NORMALIZE).
FONT = {
    " ": ("     ",) * 7,
    "!": ("  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "),
    '"': (" # # ", " # # ", "     ", "     ", "     ", "     ", "     "),
    "(": ("  #  ", " #   ", " #   ", " #   ", " #   ", " #   ", "  #  "),
    ")": ("  #  ", "   # ", "   # ", "   # ", "   # ", "   # ", "  #  "),
    "+": ("     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "),
    ",": ("     ", "     ", "     ", "     ", "  #  ", "  #  ", " #   "),
    "-": ("     ", "     ", "     ", "#####", "     ", "     ", "     "),
    ".": ("     ", "     ", "     ", "     ", "     ", "  #  ", "  #  "),
    "/": ("    #", "   # ", "   # ", "  #  ", " #   ", " #   ", "#    "),
    "0": (" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "),
    "1": ("  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "),
    "2": (" ### ", "#   #", "    #", "  ## ", " #   ", "#    ", "#####"),
    "3": ("#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "),
    "4": ("   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "),
    "5": ("#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "),
    "6": ("  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "),
    "7": ("#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "),
    "8": (" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "),
    "9": (" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "),
    ":": ("     ", "  #  ", "  #  ", "     ", "  #  ", "  #  ", "     "),
    ";": ("     ", "     ", "  #  ", "  #  ", "     ", "  #  ", " #   "),
    "=": ("     ", "     ", "#####", "     ", "#####", "     ", "     "),
    "?": (" ### ", "#   #", "    #", "  ## ", "  #  ", "     ", "  #  "),
    "A": (" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"),
    "B": ("#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "),
    "C": (" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "),
    "D": ("#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "),
    "E": ("#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"),
    "F": ("#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "),
    "G": (" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"),
    "H": ("#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"),
    "I": (" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "),
    "J": ("  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "),
    "K": ("#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"),
    "L": ("#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"),
    "M": ("#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"),
    "N": ("#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"),
    "O": (" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "),
    "P": ("#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "),
    "Q": (" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"),
    "R": ("#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"),
    "S": (" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "),
    "T": ("#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "),
    "U": ("#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "),
    "V": ("#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "),
    "W": ("#   #", "#   #", "#   #", "#   #", "# # #", "## ##", "#   #"),
    "X": ("#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"),
    "Y": ("#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "),
    "Z": ("#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"),
    "_": ("     ", "     ", "     ", "     ", "     ", "     ", "#####"),
    "æ": ("     ", " ### ", "    #", " ####", "#   #", "#  ##", " ## #"),
    "ø": ("     ", " ### ", "#   #", "# ###", "# # #", "### #", " ### "),
    "å": ("  #  ", " # # ", "  #  ", " ### ", "#   #", "#   #", " ### "),
}

# Store danske bogstaver uden egen glyph: ekspanderes.
NORMALIZE = {"Æ": "AE", "Ø": "O", "Å": "A"}


if __name__ == "__main__":
    sys.exit(main())
