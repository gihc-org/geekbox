#!/usr/bin/env python3
"""window_demo.py — vis GLES-daemonens scener i et X11-vindue (M2b, aug 2026).

Arkitektur: gles_daemon renderer offscreen og RETURNERER raa pixels over
unix-socketten ("frame"-kommandoen); denne frontend aabner et X11-vindue via
ctypes + libX11 (findes paa boksen — hverken tkinter eller PIL er noedvendigt),
pakker pixels til X' visuelle format og viser dem med XPutImage. Ved 16-bit
RGB565-visual beder frontenden daemonen om fmt="rgb565" (C-pakning), saa
Python kun vender raekkerne om — hurtig nok til demo-fart.

X skal IKKE stoppes — kun gles_daemon skal køre (GPU-stak oppe). Vinduet lukkes
med X-knappen, Escape eller naar alle frames er koert; derefter bedes daemonen
om at lukke (quit), saa der ikke bliver efterladte processer.

Brug:
  python3 window_demo.py                  # 60 frames @ 5 fps, vindue 640x360
  python3 window_demo.py --frames 120 --fps 8 --size 960x540
  python3 window_demo.py --keep-daemon    # lad daemonen blive kørende
"""
import argparse
import ctypes
import json
import socket
import struct
import sys
import time

from frontend import FONT, NORMALIZE

X11 = ctypes.CDLL("libX11.so.6")
Display_p = ctypes.c_void_p
Window_t = ctypes.c_ulong

# --- X11-funktioner med eksplicitte argtypes (32-bit ARM) ---
X11.XOpenDisplay.restype = Display_p
X11.XOpenDisplay.argtypes = [ctypes.c_char_p]
X11.XDefaultScreen.restype = ctypes.c_int
X11.XDefaultScreen.argtypes = [Display_p]
X11.XDefaultRootWindow.restype = Window_t
X11.XDefaultRootWindow.argtypes = [Display_p, ctypes.c_int]
X11.XDefaultVisual.restype = ctypes.c_void_p
X11.XDefaultVisual.argtypes = [Display_p, ctypes.c_int]
X11.XDefaultDepth.restype = ctypes.c_uint
X11.XDefaultDepth.argtypes = [Display_p, ctypes.c_int]
X11.XCreateSimpleWindow.restype = Window_t
X11.XCreateSimpleWindow.argtypes = [Display_p, Window_t, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
                                    ctypes.c_ulong, ctypes.c_ulong]
X11.XMapWindow.argtypes = [Display_p, Window_t]
X11.XStoreName.argtypes = [Display_p, Window_t, ctypes.c_char_p]
X11.XSelectInput.argtypes = [Display_p, Window_t, ctypes.c_long]
X11.XDefaultGC.restype = ctypes.c_void_p
X11.XDefaultGC.argtypes = [Display_p, ctypes.c_int]
X11.XCreateImage.restype = ctypes.c_void_p
X11.XCreateImage.argtypes = [Display_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                             ctypes.c_int, ctypes.c_char_p, ctypes.c_uint,
                             ctypes.c_uint, ctypes.c_int, ctypes.c_int]
X11.XPutImage.restype = ctypes.c_int
X11.XPutImage.argtypes = [Display_p, Window_t, ctypes.c_void_p, ctypes.c_void_p,
                          ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                          ctypes.c_uint, ctypes.c_uint]
X11.XFlush.argtypes = [Display_p]
X11.XSync.argtypes = [Display_p, ctypes.c_int]
X11.XPending.restype = ctypes.c_int
X11.XPending.argtypes = [Display_p]
X11.XNextEvent.argtypes = [Display_p, ctypes.c_void_p]
X11.XLookupKeysym.restype = ctypes.c_ulong
X11.XLookupKeysym.argtypes = [ctypes.c_void_p, ctypes.c_int]
X11.XInternAtom.restype = ctypes.c_ulong
X11.XInternAtom.argtypes = [Display_p, ctypes.c_char_p, ctypes.c_int]
X11.XSetWMProtocols.restype = ctypes.c_int
X11.XSetWMProtocols.argtypes = [Display_p, Window_t,
                                ctypes.POINTER(ctypes.c_ulong), ctypes.c_int]
X11.XCloseDisplay.argtypes = [Display_p]

Expose = 12
ClientMessage = 33
KeyPress = 2
EscapeKeysym = 0xFF1B


class Visual(ctypes.Structure):
    _fields_ = [("ext_data", ctypes.c_void_p), ("visualid", ctypes.c_ulong),
                ("class_", ctypes.c_int), ("red_mask", ctypes.c_ulong),
                ("green_mask", ctypes.c_ulong), ("blue_mask", ctypes.c_ulong),
                ("bits_per_rgb", ctypes.c_int), ("map_entries", ctypes.c_int)]


class XKeyEvent(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong),
                ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
                ("window", ctypes.c_ulong), ("root", ctypes.c_ulong),
                ("subwindow", ctypes.c_ulong), ("time", ctypes.c_ulong),
                ("x", ctypes.c_int), ("y", ctypes.c_int),
                ("x_root", ctypes.c_int), ("y_root", ctypes.c_int),
                ("state", ctypes.c_uint), ("keycode", ctypes.c_uint),
                ("same_screen", ctypes.c_int)]


class DaemonClient:
    """Unix-socket-klient: JSON-linjer ind; frame() laeser header + raa bytes."""

    def __init__(self, sock_path, timeout=20.0):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(timeout)
        self.s.connect(sock_path)

    def cmd(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        data = self.s.recv(65536)
        if not data:
            raise ConnectionError("daemonen lukkede forbindelsen")
        return json.loads(data.decode().strip())

    def frame(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        buf = b""
        idx = -1
        while idx < 0:
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError("daemonen lukkede under frame")
            buf += chunk
            idx = buf.find(b"\n")
        hdr = json.loads(buf[:idx].decode().strip())
        if not hdr.get("ok"):
            return hdr, None
        n = hdr["bytes"]
        data = bytearray(buf[idx + 1:])
        while len(data) < n:
            chunk = self.s.recv(min(65536, n - len(data)))
            if not chunk:
                raise ConnectionError("daemonen lukkede under frame-data")
            data += chunk
        return hdr, bytes(data[:n])

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def mask_info(mask):
    if not mask:
        return 0, 0
    off = (mask & -mask).bit_length() - 1
    return off, mask.bit_count()


def draw_text_rgba(buf, w, h, x, y, text, color, scale=1):
    """Tegn 5x7-tekst ind i en RGBA-buffer (raekke 0 = top)."""
    cx = x
    for ch in text:
        for c in NORMALIZE.get(ch, ch):
            glyph = FONT.get(c, FONT.get(c.upper(), FONT[" "]))
            if glyph is FONT[" "]:
                cx += 6 * scale
                continue
            for ry, row in enumerate(glyph):
                for rx, bit in enumerate(row):
                    if bit != "#":
                        continue
                    px = cx + rx * scale
                    py = y + ry * scale
                    if 0 <= px < w and 0 <= py < h:
                        off = (py * w + px) * 4
                        buf[off:off + 3] = bytes(color)
            cx += 6 * scale


def draw_text_rgb565(buf, w, h, x, y, text, color565, scale=1):
    """Tegn 5x7-tekst ind i en RGB565-bytearray (2 bytes/pixel)."""
    cx = x
    for ch in text:
        for c in NORMALIZE.get(ch, ch):
            glyph = FONT.get(c, FONT.get(c.upper(), FONT[" "]))
            if glyph is FONT[" "]:
                cx += 6 * scale
                continue
            for ry, row in enumerate(glyph):
                for rx, bit in enumerate(row):
                    if bit != "#":
                        continue
                    px = cx + rx * scale
                    py = y + ry * scale
                    if 0 <= px < w and 0 <= py < h:
                        struct.pack_into("<H", buf, (py * w + px) * 2, color565)
            cx += 6 * scale


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--socket", default="/tmp/gles.sock")
    ap.add_argument("--size", default="640x360", help="vindue/billede som WxH")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--fps", type=float, default=5.0)
    ap.add_argument("--keep-daemon", action="store_true",
                    help="send ikke quit til daemonen til sidst")
    ap.add_argument("--display", default=":0")
    args = ap.parse_args()

    try:
        w, h = (int(v) for v in args.size.split("x"))
    except ValueError:
        raise SystemExit(f"FEJL: --size skal være 'WxH', fik '{args.size}'")
    if not (0 < w <= 1920 and 0 < h <= 1080):
        raise SystemExit(f"FEJL: --size skal passe i daemonens FBO (1920x1080), fik {args.size}")

    dpy = X11.XOpenDisplay(args.display.encode())
    if not dpy:
        raise SystemExit(f"FEJL: kan ikke åbne display {args.display} — kører X?")
    screen = X11.XDefaultScreen(dpy)
    root = X11.XDefaultRootWindow(dpy, screen)
    visual_p = X11.XDefaultVisual(dpy, screen)
    depth = X11.XDefaultDepth(dpy, screen)
    if depth not in (16, 24, 32):
        raise SystemExit(f"FEJL: X-depth {depth} understøttes ikke (16/24/32)")
    visual = ctypes.cast(visual_p, ctypes.POINTER(Visual)).contents
    r_off, r_len = mask_info(visual.red_mask)
    g_off, g_len = mask_info(visual.green_mask)
    b_off, b_len = mask_info(visual.blue_mask)
    fmt = ("rgb565" if (depth == 16 and r_off == 11 and g_off == 5 and
                        b_off == 0 and r_len == 5 and g_len == 6 and b_len == 5)
           else "rgba8")
    print(f"window_demo: X-display {args.display}, depth {depth}, "
          f"masker R={visual.red_mask:08x} G={visual.green_mask:08x} "
          f"B={visual.blue_mask:08x} fmt={fmt}")

    win = X11.XCreateSimpleWindow(dpy, root, 120, 80, w, h, 1, 0x000000, 0x101418)
    X11.XStoreName(dpy, win, f"GLES-daemon demo — PowerVR G6110 ({w}x{h})".encode())
    wm_delete = X11.XInternAtom(dpy, b"WM_DELETE_WINDOW", 0)
    wm_protocols = X11.XInternAtom(dpy, b"WM_PROTOCOLS", 0)
    X11.XSetWMProtocols(dpy, win, ctypes.byref(ctypes.c_ulong(wm_delete)), 1)
    X11.XSelectInput(dpy, win, (1 << Expose) | (1 << ClientMessage) | (1 << KeyPress))
    X11.XMapWindow(dpy, win)
    X11.XFlush(dpy)

    # XPutImage-data: én bytearray (hurtig slice-pakning) + ctypes-view
    bytespp = 2 if depth == 16 else 4
    bpl = w * bytespp
    xdata = bytearray(bpl * h)
    xdata_view = (ctypes.c_ubyte * len(xdata)).from_buffer(xdata)
    image = X11.XCreateImage(dpy, visual_p, depth, 2, 0,  # ZPixmap=2, offset 0
                             ctypes.cast(xdata_view, ctypes.c_char_p), w, h,
                             32 if bytespp == 4 else 16, bpl)
    if not image:
        raise SystemExit("FEJL: XCreateImage fejlede")
    gc = X11.XDefaultGC(dpy, screen)

    client = DaemonClient(args.socket)
    fb_info = client.cmd({"cmd": "fb"})
    if not fb_info.get("ok"):
        print("window_demo: advarsel — fb-svar fejlede:", fb_info.get("error"))

    rgba = bytearray(w * h * 4)
    step = 6.283185307179586 / max(1, args.frames)
    t_next = time.monotonic()
    t_start = time.monotonic()
    keep = True
    frame_i = 0

    def _ch(v, ln):
        if ln <= 0:
            return 0
        if ln >= 8:
            return v
        return (v >> (8 - ln)) & ((1 << ln) - 1)

    def pack_frame(pixels):
        # GL-orientering (raekke 0 = bund) -> X (raekke 0 = top): vend om
        if fmt == "rgb565":
            bpr = w * 2
            for sy in range(h):
                src_off = (h - 1 - sy) * bpr
                xdata[sy * bpr:(sy + 1) * bpr] = pixels[src_off:src_off + bpr]
        elif bytespp == 4:
            for sy in range(h):
                src = rgba[(h - 1 - sy) * w * 4:(h - sy) * w * 4]
                dst_off = sy * bpl
                xdata[dst_off + 0::4] = src[2::4]
                xdata[dst_off + 1::4] = src[1::4]
                xdata[dst_off + 2::4] = src[0::4]
                xdata[dst_off + 3::4] = b"\x00" * w
        else:
            # 16-bit uden RGB565-masker: langsom fallback (forekommer ikke
            # på boksen, men koden er generel).
            for sy in range(h):
                src = rgba[(h - 1 - sy) * w * 4:(h - sy) * w * 4]
                dst_off = sy * bpl
                for sx in range(w):
                    i = sx * 4
                    v = ((_ch(src[i], r_len) << r_off) |
                         (_ch(src[i + 1], g_len) << g_off) |
                         (_ch(src[i + 2], b_len) << b_off))
                    struct.pack_into("<H", xdata, dst_off + sx * 2, v)

    try:
        while keep and frame_i < args.frames:
            while X11.XPending(dpy):
                ev = (ctypes.c_byte * 192)()
                X11.XNextEvent(dpy, ev)
                etype = ctypes.c_int.from_buffer(ev).value
                if etype == ClientMessage:
                    keep = False
                elif etype == KeyPress:
                    key = ctypes.cast(ev, ctypes.POINTER(XKeyEvent)).contents
                    if X11.XLookupKeysym(ev, 0) == EscapeKeysym:
                        keep = False
            if not keep:
                break

            phase = frame_i * step
            hdr, pixels = client.frame(
                {"cmd": "frame", "scene": "triangle", "rect": [0, 0, w, h],
                 "phase": round(phase, 3), "fmt": fmt})
            if not hdr.get("ok"):
                print("window_demo: frame fejlede:", hdr.get("error"))
                break
            ms = hdr.get("ms", 0.0)
            label = f"GLES via daemon — frame {frame_i + 1} — {ms:.0f} ms"
            if fmt == "rgb565":
                pack_frame(pixels)
                draw_text_rgb565(xdata, w, h, 8, 6, label, 0xFFFF)
            else:
                rgba[:] = pixels
                draw_text_rgba(rgba, w, h, 8, 6, label, (255, 255, 255))
                pack_frame(None)
            X11.XPutImage(dpy, win, gc, image, 0, 0, 0, 0, w, h)
            X11.XFlush(dpy)
            frame_i += 1
            t_next += 1.0 / args.fps
            delay = t_next - time.monotonic()
            if delay > 0:
                time.sleep(delay)
    except (ConnectionError, socket.timeout) as e:
        print(f"window_demo: FEJL — {e}")
        keep = False
    finally:
        if not args.keep_daemon:
            try:
                client.cmd({"cmd": "quit"})
            except OSError:
                pass
        client.close()
        X11.XCloseDisplay(dpy)

    t_el = time.monotonic() - t_start
    if t_el > 0:
        print(f"window_demo: færdig efter {frame_i} frames på {t_el:.1f} s "
              f"({frame_i / t_el:.1f} fps)")
    else:
        print(f"window_demo: færdig efter {frame_i} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
