// xdraw_probe.cpp — X-tegneprobe: tro C-klon af window_demo.py (event-loop +
// gentagen XPutImage), parameteriseret. Bruges til at finde hvorfor C-klienten
// ikke tegner, mens Python-window_demo gør.
// Bruges til at isolere om hybris-load i processen ødelægger X-tegning:
//   g++ -O2 -o xdraw_plain xdraw_probe.cpp -lX11
//   g++ -O2 -o xdraw_hybris xdraw_probe.cpp -lX11 \
//       -L/opt/hybris -Wl,-rpath-link,/opt/hybris -lEGL -lGLESv2 -lhybris-common
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int main(int argc, char **argv)
{
    int wx = 120, wy = 80, w = 320, h = 180, repeat = 1, settle = 0;
    int secondconn = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pos") && i + 2 < argc) { wx = atoi(argv[++i]); wy = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { w = atoi(argv[++i]); h = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--repeat")) repeat = 1;
        else if (!strcmp(argv[i], "--settle")) settle = 1;
        else if (!strcmp(argv[i], "--secondconn")) secondconn = 1;
    }
    const char *dname = getenv("DISPLAY");
    Display *dpy = XOpenDisplay(dname && *dname ? dname : ":0");
    if (!dpy) { fprintf(stderr, "XOpenDisplay fejlede\n"); return 1; }
    int scr = DefaultScreen(dpy);
    Window xid = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), wx, wy,
                                     w, h, 1, 0x0000, 0x101418);
    XStoreName(dpy, xid, "xdraw_probe");
    XMapWindow(dpy, xid);
    XSelectInput(dpy, xid, ExposureMask | ClientMessage | KeyPress);
    XFlush(dpy);
    if (settle) {
        sleep(2);
        while (XPending(dpy)) { XEvent e; XNextEvent(dpy, &e); }
        printf("xdraw_probe: settle færdig\n");
        fflush(stdout);
    }
    std::vector<unsigned char> px((size_t)w * h * 2);
    for (size_t i = 0; i < px.size(); i += 2) { px[i] = 0xE0; px[i + 1] = 0x07; }
    Display *draw_dpy = dpy;
    if (secondconn) {
        draw_dpy = XOpenDisplay(dname && *dname ? dname : ":0");
        if (!draw_dpy) { fprintf(stderr, "anden forbindelse fejlede\n"); return 1; }
        XSelectInput(draw_dpy, xid, ExposureMask);
        printf("xdraw_probe: tegner fra ANDEN forbindelse (%p)\n", (void *)draw_dpy);
        fflush(stdout);
    }
    for (int n = 0; n < 20; n++) {
        /* dræn events (Expose m.m.) før tegning — som window_demo */
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
        }
        XImage *img = XCreateImage(draw_dpy, DefaultVisual(draw_dpy, 0), 16, ZPixmap, 0,
                                   (char *)&px[0], w, h, 16, w * 2);
        if (img) {
            XPutImage(draw_dpy, xid, DefaultGC(draw_dpy, 0), img, 0, 0, 0, 0, w, h);
            XFlush(draw_dpy);
            img->data = NULL;
            XDestroyImage(img);
        }
        if (!repeat) break;
        struct timespec ts = { 0, 150 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    printf("xdraw_probe: %d tegninger sendt (%dx%d+%d+%d)\n", repeat ? 20 : 1, w, h, wx, wy);
    fflush(stdout);
    sleep(2);
    XCloseDisplay(dpy);
    return 0;
}
