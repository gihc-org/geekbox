#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Probe 4: replikerer Firefox' vinduesstruktur så tæt som muligt:
 *   root(16) -> openbox-ramme(32) -> client/Navigator(32, sort bg)
 *   -> EGL-barn(32, sort bg) tegnet med XPutImage fra EN ANDEN X-forbindelse,
 *   gentagne frames med XSync efter hver (som eglplatform_x11 put_ximage).
 * Kør: gcc -o x32probe4 x32probe4.c -lX11 && DISPLAY=:0 ./x32probe4
 * Dump root/fb0/rammen imens for at se om indholdet når skærmen. */
static XVisualInfo find_visual(Display *d, unsigned long vid) {
    XVisualInfo tpl; int n;
    tpl.visualid = vid;
    XVisualInfo *list = XGetVisualInfo(d, VisualIDMask, &tpl, &n);
    XVisualInfo out;
    memset(&out, 0, sizeof(out));
    if (n > 0) { out = list[0]; XFree(list); }
    return out;
}

static void put_pixels(Display *d, Window w, Visual *v, int depth, int W, int H,
                       int frame, int separate_gc) {
    unsigned char *buf = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        unsigned int r, g, b;
        if (frame % 2 == 0) { r = 0x00; g = 0xff; b = 0x00; }   /* grøn */
        else                { r = 0x00; g = 0x00; b = 0xff; }   /* blå   */
        buf[(y*W+x)*4+0] = b; buf[(y*W+x)*4+1] = g; buf[(y*W+x)*4+2] = r; buf[(y*W+x)*4+3] = 0xff;
    }
    XImage *img = XCreateImage(d, v, depth, ZPixmap, 0, (char*)buf, W, H, 32, W * 4);
    GC gc = XCreateGC(d, w, 0, NULL);
    XPutImage(d, w, gc, img, 0, 0, 0, 0, W, H);
    XFreeGC(d, gc);
    img->data = NULL;
    XDestroyImage(img);
    free(buf);
    (void)separate_gc;
}

static unsigned long read_pixel(Display *d, Window w, int x, int y) {
    XImage *im = XGetImage(d, w, x, y, 1, 1, AllPlanes, ZPixmap);
    unsigned long v = XGetPixel(im, 0, 0);
    XDestroyImage(im);
    return v;
}

static void print_attrs(Display *d, Window w, const char *label) {
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, w, &a)) { printf("%s: XGetWindowAttributes fejlede\n", label); return; }
    printf("%s: depth=%d visual=%#lx class=%d map=%d backing=%d save=%d backing_pix=%#lx\n",
           label, a.depth, a.visual->visualid, a.class, a.map_state,
           a.backing_store, a.save_under, a.backing_pixel);
}

int main() {
    unsigned long vid = 0x1ec;
    Display *owner = XOpenDisplay(NULL);
    Display *painter = XOpenDisplay(NULL);
    int scr = DefaultScreen(owner);
    Window root = RootWindow(owner, scr);
    XVisualInfo vi = find_visual(owner, vid);
    if (!vi.visual) { printf("visual %#lx ikke fundet\n", vid); return 1; }
    printf("visual %#lx depth %d\n", vi.visualid, vi.depth);

    XSetWindowAttributes attrs;
    attrs.colormap = XCreateColormap(owner, root, vi.visual, AllocNone);
    attrs.background_pixel = 0x000000;   /* sort, som Firefox' Navigator */
    attrs.border_pixel = 0;
    attrs.override_redirect = False;
    Window client = XCreateWindow(owner, root, 600, 350, 300, 200, 0,
        vi.depth, InputOutput, vi.visual,
        CWColormap|CWBackPixel|CWBorderPixel|CWOverrideRedirect, &attrs);
    Window child = XCreateWindow(owner, client, 5, 5, 200, 150, 0,
        vi.depth, InputOutput, vi.visual,
        CWColormap|CWBackPixel|CWOverrideRedirect, &attrs);
    XMapWindow(owner, child);
    XMapWindow(owner, client);
    XSync(owner, False);
    usleep(500000);
    print_attrs(owner, client, "client");
    print_attrs(owner, child, "child ");

    /* find clientens absolutte position */
    Window ret;
    int xr, yr;
    XTranslateCoordinates(owner, client, root, 0, 0, &xr, &yr, &ret);
    printf("client absolut: (%d,%d)\n", xr, yr);

    /* 10 frames, grøn/blå skiftevis, XSync efter hver (som modulet) */
    for (int f = 0; f < 10; f++) {
        put_pixels(painter, child, vi.visual, vi.depth, 200, 150, f, 0);
        XSync(painter, False);
        usleep(150000);
        unsigned long p1 = read_pixel(painter, root, xr + 60, yr + 60);
        unsigned long p2 = read_pixel(painter, root, xr + 250, yr + 150);
        printf("frame %d: root(child-px)=%#06lx root(parent-px)=%#06lx\n", f, p1, p2);
        fflush(stdout);
    }
    sleep(5);
    return 0;
}
