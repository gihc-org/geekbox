/* x_resize.c - saet storrelse/position paa Firefox-vinduet (til fps-tests).
 *
 * Baggrund (17. sep 2026): readback-proben viser at glReadPixels af
 * 1920x1080 tager 173 ms (maks 5,8 fps) mens 836x470 tager 34 ms. Med
 * software-layers gaar browserens composite gennem den vej, saa
 * vinduesstorrelsen saetter et haardt loft for fps. Dette vaerktoj aendrer
 * vinduet under koersel, saa vi kan maale forskellen uden at genstarte.
 *
 * Byg/koer paa boksen:
 *   gcc -O2 -o /root/x_resize /root/x_resize.c -lX11
 *   DISPLAY=:0 /root/x_resize firefox 1000 600 [x y]
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Window find_window(Display *d, Window w, const char *needle, int depth)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0;
    char *name = NULL;
    if (XFetchName(d, w, &name) && name) {
        if (strcasestr(name, needle)) {
            XFree(name);
            return w;
        }
        XFree(name);
    }
    XClassHint ch;
    memset(&ch, 0, sizeof ch);
    if (XGetClassHint(d, w, &ch)) {
        int hit = 0;
        if (ch.res_name && strcasestr(ch.res_name, needle))
            hit = 1;
        if (ch.res_class && strcasestr(ch.res_class, needle))
            hit = 1;
        if (ch.res_name)
            XFree(ch.res_name);
        if (ch.res_class)
            XFree(ch.res_class);
        if (hit)
            return w;
    }
    if (depth > 6 || !XQueryTree(d, w, &root, &parent, &kids, &n) || !kids)
        return 0;
    Window found = 0;
    for (unsigned i = 0; i < n && !found; i++)
        found = find_window(d, kids[i], needle, depth + 1);
    XFree(kids);
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "brug: %s <wmklasse> <bredde> <hoejde> [x y]\n", argv[0]);
        return 2;
    }
    const char *needle = argv[1];
    int w = atoi(argv[2]), h = atoi(argv[3]);
    int x = argc > 5 ? atoi(argv[4]) : 0;
    int y = argc > 5 ? atoi(argv[5]) : 0;
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "kan ikke aabne DISPLAY\n");
        return 1;
    }
    Window win = find_window(d, DefaultRootWindow(d), needle, 0);
    if (!win) {
        fprintf(stderr, "fandt intet vindue med '%s'\n", needle);
        return 2;
    }
    XMoveResizeWindow(d, win, x, y, (unsigned)w, (unsigned)h);
    XFlush(d);
    XSync(d, False);
    XWindowAttributes at;
    XGetWindowAttributes(d, win, &at);
    printf("vindue 0x%lx sat til %dx%d+%d+%d (er nu %dx%d)\n",
           (unsigned long)win, w, h, x, y, at.width, at.height);
    XCloseDisplay(d);
    return 0;
}
