#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
/* Skærmdump: XGetImage på root (eller angivet vindue) → rå pixels på stdout.
 * Kør: gcc -o /tmp/xdump /tmp/xdump.c -lX11
 *      DISPLAY=:0 /tmp/xdump > skærm.raw 2>info.txt   (default root)
 *      DISPLAY=:0 /tmp/xdump 0x100003c > vindue.raw   (angivet vindue)
 * NB: XGetImage fejler med BadMatch på nogle depth-32 Navigator-vinduer —
 * dump da i stedet barnet eller root. */
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    Window w = DefaultRootWindow(d);
    if (argc > 1) w = (Window)strtoul(argv[1], NULL, 0);
    XWindowAttributes a;
    XGetWindowAttributes(d, w, &a);
    XImage *im = XGetImage(d, w, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
    if (!im) { fprintf(stderr, "XGetImage fail\n"); return 1; }
    fprintf(stderr, "%dx%d bpp=%d\n", im->width, im->height, im->bits_per_pixel);
    fwrite(im->data, 1, im->bytes_per_line * im->height, stdout);
    return 0;
}
