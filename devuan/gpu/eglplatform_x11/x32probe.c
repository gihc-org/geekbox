#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Probe: kan X-serveren kompositerer 32-bit vinduer/children til 16-bit root?
 * Kør: gcc -o x32probe x32probe.c -lX11 && DISPLAY=:0 ./x32probe
 * Målt 25. aug 2026: JA — grønt barn + rød parent synlige på root (0x07e0/0xf800). */
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
        if (depth == 32) {
            unsigned int r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
            buf[(y*W+x)*4+0] = b; buf[(y*W+x)*4+1] = g; buf[(y*W+x)*4+2] = r; buf[(y*W+x)*4+3] = 0xff;
        }
    }
    XImage *img = XCreateImage(d, v, depth, ZPixmap, 0, (char*)buf, W, H,
                               (depth == 32) ? 32 : 16, W * bpp);
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
    if (!d) { printf("no display\n"); return 1; }
    int scr = DefaultScreen(d);
    Window root = RootWindow(d, scr);
    XVisualInfo vi32;
    if (!find32(d, &vi32)) { printf("ingen 32-bit visual\n"); return 1; }
    printf("32-bit visual: %#lx depth %d\n", vi32.visualid, vi32.depth);

    XSetWindowAttributes attrs;
    attrs.colormap = XCreateColormap(d, root, vi32.visual, AllocNone);
    attrs.background_pixel = 0xff0000;
    attrs.border_pixel = 0;
    attrs.override_redirect = True;
    Window parent = XCreateWindow(d, root, 100, 100, 300, 200, 0,
        vi32.depth, InputOutput, vi32.visual,
        CWColormap|CWBackPixel|CWBorderPixel|CWOverrideRedirect, &attrs);
    Window child = XCreateWindow(d, parent, 20, 20, 150, 100, 0,
        vi32.depth, InputOutput, vi32.visual,
        CWColormap|CWBackPixel|CWOverrideRedirect, &attrs);
    XMapWindow(d, child);
    XMapWindow(d, parent);
    XSync(d, False);
    put_pixels(d, child, vi32.visual, 32, 150, 100, 0x00ff00);
    XSync(d, False);
    printf("parent (100,100) 300x200, barn (20,20) 150x100, grønt barn\n");
    printf("root(150,150) = %#06lx (grønt hvis kompositering virker)\n", read_pixel(d, root, 150, 150));
    printf("root(120,120) = %#06lx (rødt = parent-baggrund)\n", read_pixel(d, root, 120, 120));
    printf("parent(60,60)  = %#06lx\n", read_pixel(d, parent, 60, 60));
    printf("child(10,10)   = %#06lx\n", read_pixel(d, child, 10, 10));
    put_pixels(d, parent, vi32.visual, 32, 300, 200, 0x0000ff);
    XSync(d, False);
    printf("efter blåt i parent: root(150,150) = %#06lx (barn dækker: grønt)\n", read_pixel(d, root, 150, 150));
    printf("root(120,120) = %#06lx (blåt)\n", read_pixel(d, root, 120, 120));
    sleep(2);
    return 0;
}
