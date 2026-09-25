/* xput_bench.c — hvor hurtigt kan en almindelig X-klient praesentere et billede?
 * (25. sep 2026, fps-sporet.)
 *
 * Baggrund: fb_bench viste at CPU'en kan skrive et helt 1080p-frame til
 * /dev/fb0 paa ~3 ms. Alligevel leverer Firefox' software-kompositor kun
 * ~2,4 frames/s for et fuldt vindue. Dette værktøj maaler X-serverens egen
 * praesentationsvej uden browseren: XPutImage (og XShmPutImage hvis muligt)
 * i et vindue af given stoerrelse, med XSync pr. frame (fuld round-trip).
 *
 * Brug:
 *   gcc -O2 -Wall -o xput_bench xput_bench.c -lX11 -lXext
 *   DISPLAY=:0 ./xput_bench 1920 1054 16 20      # depth 16 som Firefox
 *   DISPLAY=:0 ./xput_bench 1920 1054 32 20      # ARGB-vindue (hvis visual)
 *   DISPLAY=:0 ./xput_bench 640 360 16 20
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* XShm er valgfrit: boksen har ikke libxext-udviklingsfilerne (25. sep 2026),
 * saa XPutImage-vejen maales altid, og SHM kun hvor headeren findes. */
#if defined(__has_include)
#  if __has_include(<X11/extensions/XShm.h>)
#    define HAVE_XSHM 1
#  endif
#endif
#ifdef HAVE_XSHM
#  include <sys/ipc.h>
#  include <sys/shm.h>
#  include <X11/extensions/XShm.h>
#endif

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 1920;
    int h = argc > 2 ? atoi(argv[2]) : 1054;
    int depth = argc > 3 ? atoi(argv[3]) : 16;
    int iters = argc > 4 ? atoi(argv[4]) : 20;

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "kan ikke aabne display (saet DISPLAY)\n");
        return 1;
    }
    int scr = DefaultScreen(dpy);
    XVisualInfo vi;
    if (!XMatchVisualInfo(dpy, scr, depth, TrueColor, &vi)) {
        fprintf(stderr, "ingen TrueColor-visual med depth %d\n", depth);
        return 1;
    }
    Window root = RootWindow(dpy, scr);
    Colormap cmap = (vi.visual == DefaultVisual(dpy, scr))
                        ? CopyFromParent
                        : XCreateColormap(dpy, root, vi.visual, AllocNone);
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof swa);
    swa.colormap = cmap;
    swa.background_pixel = 0x101418;
    swa.event_mask = ExposureMask;
    Window win = XCreateWindow(dpy, root, 0, 0, w, h, 0, depth, InputOutput,
                               vi.visual, CWColormap | CWBackPixel | CWEventMask,
                               &swa);
    XStoreName(dpy, win, "xput_bench");
    XMapWindow(dpy, win);
    XSync(dpy, False);
    sleep(1);

    /* XVisualInfo har ikke bits_per_pixel; ZPixmap-bredden foelger depth:
     * 16 -> 16 bpp (RGB565), 24/32 -> 32 bpp. */
    int bpp = (depth == 15 || depth == 16) ? 16 : (depth == 8 ? 8 : 32);
    size_t rowbytes = (size_t)w * (bpp / 8);
    size_t bytes = rowbytes * h;
    char *data = malloc(bytes);
    if (!data) {
        fprintf(stderr, "ikke nok hukommelse\n");
        return 1;
    }
    for (size_t i = 0; i < bytes; i++)
        data[i] = (char)(i * 5 + (i >> 7));

    printf("vindue %dx%d depth=%d bpp=%d, %d frames pr. test\n", w, h, depth,
           bpp, iters);

    XImage *img = XCreateImage(dpy, vi.visual, (unsigned)depth, ZPixmap, 0,
                               data, (unsigned)w, (unsigned)h, 32,
                               (int)rowbytes);
    if (!img) {
        fprintf(stderr, "XCreateImage fejlede\n");
        return 1;
    }
    GC gc = DefaultGC(dpy, scr);

    double t0 = now_ms();
    for (int i = 0; i < iters; i++)
        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XSync(dpy, False);
    double ms = (now_ms() - t0) / iters;
    printf("XPutImage (køet, uden sync pr. frame): %7.2f ms/frame  %8.1f MB/s\n",
           ms, bytes / 1048576.0 / (ms / 1000.0));

    t0 = now_ms();
    for (int i = 0; i < iters; i++) {
        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
        XSync(dpy, False);
    }
    ms = (now_ms() - t0) / iters;
    printf("XPutImage (XSync pr. frame):        %7.2f ms/frame  %8.3f us/pixel "
           " %6.1f fps\n", ms, ms * 1000.0 / ((double)w * h), 1000.0 / ms);

#ifdef HAVE_XSHM
    int shmok = 0;
    XShmSegmentInfo shminfo;
    XImage *simg = NULL;
    if (XShmQueryExtension(dpy)) {
        shminfo.shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
        if (shminfo.shmid >= 0) {
            shminfo.shmaddr = shmat(shminfo.shmid, NULL, 0);
            shminfo.readOnly = False;
            if (shminfo.shmaddr != (char *)-1 &&
                XShmAttach(dpy, &shminfo)) {
                memcpy(shminfo.shmaddr, data, bytes);
                simg = XShmCreateImage(dpy, vi.visual, (unsigned)depth, ZPixmap,
                                       shminfo.shmaddr, &shminfo, (unsigned)w,
                                       (unsigned)h);
                if (simg) {
                    t0 = now_ms();
                    for (int i = 0; i < iters; i++) {
                        XShmPutImage(dpy, win, gc, simg, 0, 0, 0, 0,
                                     (unsigned)w, (unsigned)h, False);
                        XSync(dpy, False);
                    }
                    ms = (now_ms() - t0) / iters;
                    printf("XShmPutImage (XSync pr. frame):     %7.2f ms/frame  "
                           "%8.3f us/pixel  %6.1f fps\n",
                           ms, ms * 1000.0 / ((double)w * h), 1000.0 / ms);
                    shmok = 1;
                }
            }
        }
    }
    if (!shmok)
        printf("XShmPutImage: utilgaengelig\n");
#else
    printf("XShmPutImage: ikke kompileret ind (mangler XShm.h)\n");
#endif

    fflush(stdout);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
