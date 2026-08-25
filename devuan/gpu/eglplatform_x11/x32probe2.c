#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Probe 2: 32-bit vindue der bliver openbox-framet (som Firefox) + 32-bit barn
 * tegnet med XPutImage. Kør: gcc -o x32probe2 x32probe2.c -lX11 &&
 * DISPLAY=:0 ./x32probe2  (dump root + fb0 imens)
 * Målt 25. aug 2026: grønt barn + rød parent når BÅDE root og fb0
 * (0x07e0/0xf800) — openbox-rammen bryder IKKE 32-bit-kompositeringen. */
static int find32(Display *d, XVisualInfo *out) {
    XVisualInfo tpl; int n;
    tpl.screen = DefaultScreen(d);
    tpl.depth = 32;
    tpl.class = TrueColor;
    XVisualInfo *list = XGetVisualInfo(d, VisualScreenMask|VisualDepthMask|VisualClassMask, &tpl, &n);
    if (n > 0) { *out = list[0]; XFree(list); return 1; }
    return 0;
}

static void put_pixels(Display *d, Window w, Visual *v, int depth, int W, int H, unsigned int color) {
    int bpp = (depth == 32) ? 4 : 2;
    unsigned char *buf = malloc((size_t)W * H * bpp);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        unsigned int r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
        buf[(y*W+x)*4+0] = b; buf[(y*W+x)*4+1] = g; buf[(y*W+x)*4+2] = r; buf[(y*W+x)*4+3] = 0xff;
    }
    XImage *img = XCreateImage(d, v, depth, ZPixmap, 0, (char*)buf, W, H, 32, W * 4);
    GC gc = XCreateGC(d, w, 0, NULL);
    XPutImage(d, w, gc, img, 0, 0, 0, 0, W, H);
    XFreeGC(d, gc);
    img->data = NULL;
    XDestroyImage(img);
    free(buf);
}

static unsigned long read_pixel(Display *d, Window w, int x, int y) {
    XImage *im = XGetImage(d, w, x, y, 1, 1, AllPlanes, ZPixmap);
    unsigned long v = XGetPixel(im, 0, 0);
    XDestroyImage(im);
    return v;
}

int main() {
    Display *d = XOpenDisplay(NULL);
    int scr = DefaultScreen(d);
    Window root = RootWindow(d, scr);
    XVisualInfo vi32;
    find32(d, &vi32);

    XSetWindowAttributes attrs;
    attrs.colormap = XCreateColormap(d, root, vi32.visual, AllocNone);
    attrs.background_pixel = 0xff0000;
    attrs.border_pixel = 0;
    attrs.override_redirect = False;   /* lad openbox frame det */
    Window parent = XCreateWindow(d, root, 500, 300, 300, 200, 0,
        vi32.depth, InputOutput, vi32.visual,
        CWColormap|CWBackPixel|CWBorderPixel|CWOverrideRedirect, &attrs);
    Window child = XCreateWindow(d, parent, 20, 20, 150, 100, 0,
        vi32.depth, InputOutput, vi32.visual,
        CWColormap|CWBackPixel|CWOverrideRedirect, &attrs);
    XMapWindow(d, child);
    XMapWindow(d, parent);
    XSync(d, False);
    usleep(300000);
    put_pixels(d, child, vi32.visual, 32, 150, 100, 0x00ff00);
    XSync(d, False);

    Window parent_now = parent, root_return;
    int xr, yr;
    unsigned int wr, hr, bw, dep;
    XGetGeometry(d, parent, &root_return, &xr, &yr, &wr, &hr, &bw, &dep);
    printf("client-pos på root: (%d,%d) %ux%u depth %u\n", xr, yr, wr, hr, dep);
    printf("root(%d,%d) = %#06lx (grønt barn?)\n", xr+60, yr+60, read_pixel(d, root, xr+60, yr+60));
    printf("root(%d,%d) = %#06lx (rød parent?)\n", xr+250, yr+150, read_pixel(d, root, xr+250, yr+150));
    printf("client(60,60) = %#010lx\n", read_pixel(d, parent, 60, 60));
    sleep(12);
    return 0;
}
