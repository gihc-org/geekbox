#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Probe 3: visual kan vælges (arg; default 0x1ec = Firefox' visual), og tegneren
 * bruger en ANDEN X-forbindelse end den der ejer vinduet — simulerer en
 * GPU-proces-genstart, hvor det nye GPU-proces forbindelse XPutImage-tter ind
 * i et vindue ejet af main-processens forbindelse.
 * Kør: gcc -o x32probe3 x32probe3.c -lX11 && DISPLAY=:0 ./x32probe3 [visualid]
 * Målt 25. aug 2026: (køres under skrivning af session-notatet). */
static XVisualInfo find_visual(Display *d, unsigned long vid) {
    XVisualInfo tpl; int n;
    tpl.visualid = vid;
    XVisualInfo *list = XGetVisualInfo(d, VisualIDMask, &tpl, &n);
    XVisualInfo out;
    memset(&out, 0, sizeof(out));
    if (n > 0) { out = list[0]; XFree(list); }
    return out;
}

static void put_pixels(Display *d, Window w, Visual *v, int depth, int W, int H, unsigned int color) {
    unsigned char *buf = malloc((size_t)W * H * 4);
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

int main(int argc, char **argv) {
    unsigned long vid = (argc > 1) ? strtoul(argv[1], NULL, 0) : 0x1ec;
    Display *owner = XOpenDisplay(NULL);       /* forbindelse A: ejer vinduet */
    Display *painter = XOpenDisplay(NULL);     /* forbindelse B: tegner (simulerer GPU-genstart) */
    int scr = DefaultScreen(owner);
    Window root = RootWindow(owner, scr);
    XVisualInfo vi = find_visual(owner, vid);
    if (!vi.visual) { printf("visual %#lx ikke fundet\n", vid); return 1; }
    printf("visual %#lx depth %d\n", vi.visualid, vi.depth);

    XSetWindowAttributes attrs;
    attrs.colormap = XCreateColormap(owner, root, vi.visual, AllocNone);
    attrs.background_pixel = 0xff0000;
    attrs.border_pixel = 0;
    attrs.override_redirect = False;
    Window parent = XCreateWindow(owner, root, 400, 400, 300, 200, 0,
        vi.depth, InputOutput, vi.visual,
        CWColormap|CWBackPixel|CWBorderPixel|CWOverrideRedirect, &attrs);
    Window child = XCreateWindow(owner, parent, 20, 20, 150, 100, 0,
        vi.depth, InputOutput, vi.visual,
        CWColormap|CWBackPixel|CWOverrideRedirect, &attrs);
    XMapWindow(owner, child);
    XMapWindow(owner, parent);
    XSync(owner, False);
    usleep(400000);

    /* tegneren (forbindelse B) tegner grønt i barnet via XPutImage */
    put_pixels(painter, child, vi.visual, vi.depth, 150, 100, 0x00ff00);
    XSync(painter, False);

    Window r, ret;
    int xr, yr;
    unsigned int wr, hr, bw, dep;
    XGetGeometry(owner, parent, &ret, &xr, &yr, &wr, &hr, &bw, &dep);
    /* absolut position via translate */
    XTranslateCoordinates(owner, parent, root, 0, 0, &xr, &yr, &r);
    printf("client absolut: (%d,%d) %ux%u\n", xr, yr, wr, hr);
    printf("root(%d,%d) = %#06lx (grønt barn?)\n", xr+60, yr+60, read_pixel(painter, root, xr+60, yr+60));
    printf("root(%d,%d) = %#06lx (rød parent?)\n", xr+250, yr+150, read_pixel(painter, root, xr+250, yr+150));
    sleep(12);
    return 0;
}
