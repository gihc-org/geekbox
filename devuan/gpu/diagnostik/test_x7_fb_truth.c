/* test_x7_fb_truth.c — grundsandhedstest: tegner X overhovedet på skærmen?
 *
 * Baggrund (M2b, aug 2026): XPutImage/XFillRectangle virker "uden fejl",
 * men XGetImage læser alt sort tilbage. Denne test afgør om serveren
 * tegner korrekt ved at læse /dev/fb0 (den faktiske skærmbuffer) direkte
 * med mmap samtidig med at der tegnes i X.
 *
 * Byg/afvikling på boksen:
 *   gcc -O2 -o /tmp/test_x7 /tmp/test_x7.c -lX11
 *   DISPLAY=:0 /tmp/test_x7
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int err_count = 0;
static int err_handler(Display *d, XErrorEvent *e)
{
    err_count++;
    char buf[256];
    XGetErrorText(d, e->error_code, buf, sizeof buf);
    fprintf(stderr, "X-FEJL: %s (request %d)\n", buf, e->request_code);
    return 0;
}

static int fb_fd = -1;
static unsigned char *fb = NULL;
static struct fb_var_screeninfo fb_vi;
static struct fb_fix_screeninfo fb_fi;

static uint16_t fb_pixel(int x, int y)
{
    size_t off = (size_t)y * fb_fi.line_length + (size_t)x * 2;
    uint16_t v;
    memcpy(&v, fb + off, 2);
    return v;
}

/* tæl pixels == want i rektanglet; sampler hver 31. pixel i hver retning */
static void count_pixels(int x, int y, int w, int h, uint16_t want,
                         const char *label)
{
    int total = 0, hit = 0;
    for (int yy = y; yy < y + h; yy += 31) {
        for (int xx = x; xx < x + w; xx += 31) {
            total++;
            if (fb_pixel(xx, yy) == want)
                hit++;
        }
    }
    printf("%s: %d/%d sample-pixels == 0x%04x\n", label, hit, total, want);
}

int main(void)
{
    FILE *vt = fopen("/sys/class/tty/tty0/active", "r");
    char act[16] = "?";
    if (vt) {
        if (fgets(act, sizeof act, vt))
            act[strcspn(act, "\n")] = 0;
        fclose(vt);
    }
    printf("aktiv VT: %s (X kører på vt9)\n", act);
    if (strcmp(act, "tty9") != 0)
        system("chvt 9");

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("/dev/fb0"); return 1; }
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_vi);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fi);
    fb = mmap(NULL, (size_t)fb_fi.line_length * fb_vi.yres,
              PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb == MAP_FAILED) { perror("mmap fb0"); return 1; }
    printf("fb0: %ux%u stride=%u bpp=%u\n",
           fb_vi.xres, fb_vi.yres, fb_fi.line_length, fb_vi.bits_per_pixel);

    Display *dpy = XOpenDisplay(":0");
    if (!dpy) { fprintf(stderr, "kan ikke åbne :0\n"); return 1; }
    XSetErrorHandler(err_handler);
    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);
    GC gc = DefaultGC(dpy, scr);
    Visual *vis = DefaultVisual(dpy, scr);
    int depth = DefaultDepth(dpy, scr);
    printf("X: depth=%d\n", depth);

    /* Områder: vindue (200,150,400x300), root-fyld (700,450,300x200) */
    count_pixels(200, 150, 400, 300, 0xffff, "FØR  vindue (hvid)");
    count_pixels(700, 450, 300, 200, 0xffff, "FØR  root  (hvid)");
    count_pixels(700, 450, 300, 200, 0xf81f, "FØR  root  (blå)");

    Window win = XCreateSimpleWindow(dpy, root, 200, 150, 400, 300,
                                     1, 0x000000, 0xffff);
    XMapWindow(dpy, win);
    XSync(dpy, False);
    sleep(1);
    count_pixels(200, 150, 400, 300, 0xffff, "EFTER vindue map (hvid bg)");

    XSetForeground(dpy, gc, 0xffff);
    XFillRectangle(dpy, root, gc, 700, 450, 300, 200);
    XSync(dpy, False);
    sleep(1);
    count_pixels(700, 450, 300, 200, 0xffff, "EFTER XFillRect root");
    XImage *rg = XGetImage(dpy, root, 700, 450, 10, 10, AllPlanes, ZPixmap);
    printf("XGetImage root efter fill: 0x%04lx\n",
           rg ? XGetPixel(rg, 5, 5) : 0);

    unsigned char *buf = malloc(640 * 360 * 2);
    for (int i = 0; i < 640 * 360 * 2; i += 2) { buf[i] = 0x1f; buf[i + 1] = 0xf8; }
    XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0, (char *)buf,
                               640, 360, 16, 1280);
    XPutImage(dpy, root, gc, img, 0, 0, 700, 450, 300, 200);
    XSync(dpy, False);
    sleep(1);
    count_pixels(700, 450, 300, 200, 0xf81f, "EFTER XPutImage root");
    XImage *rp = XGetImage(dpy, root, 700, 450, 10, 10, AllPlanes, ZPixmap);
    printf("XGetImage root efter put: 0x%04lx\n",
           rp ? XGetPixel(rp, 5, 5) : 0);

    XSetForeground(dpy, gc, 0xffff);
    XFillRectangle(dpy, win, gc, 0, 0, 400, 300);
    XSync(dpy, False);
    sleep(1);
    count_pixels(200, 150, 400, 300, 0xffff, "EFTER XFillRect i vindue");
    XImage *rw = XGetImage(dpy, win, 0, 0, 10, 10, AllPlanes, ZPixmap);
    printf("XGetImage vindue efter fill: 0x%04lx\n",
           rw ? XGetPixel(rw, 5, 5) : 0);

    printf("X-fejl i alt: %d\n", err_count);
    sleep(1);
    return 0;
}
