/* test_x8_putimage.c — skarpere M2b-test: rydder gamle testvinduer,
 * afgør om XPutImage overhovedet tegner, og dumper fb0 til visuel kontrol.
 *
 * Områder:
 *   A vindue         (100,80, 480x270) — hvid baggrund ved map
 *   B root fill hvid (700,400, 300x200)
 *   C root put f81f  (1000,700, 300x200)
 *   D put f81f i vindue (140,120, 200x150)
 *
 * Byg/afvikling:
 *   gcc -O2 -o /tmp/test_x8 /tmp/test_x8.c -lX11 && /tmp/test_x8
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

static unsigned char *fb;
static struct fb_var_screeninfo fb_vi;
static struct fb_fix_screeninfo fb_fi;

static uint16_t fb_pixel(int x, int y)
{
    uint16_t v;
    memcpy(&v, fb + (size_t)y * fb_fi.line_length + (size_t)x * 2, 2);
    return v;
}

static void region(int x, int y, int w, int h, const char *label)
{
    int total = 0, white = 0, blue = 0, swp = 0, other = 0;
    for (int yy = y; yy < y + h; yy += 31) {
        for (int xx = x; xx < x + w; xx += 31) {
            uint16_t v = fb_pixel(xx, yy);
            total++;
            if (v == 0xffff) white++;
            else if (v == 0xf81f) blue++;
            else if (v == 0x1ff8) swp++;
            else other++;
        }
    }
    printf("%-28s hvid=%d blå=%d byttet=%d andet=%d (af %d)\n",
           label, white, blue, swp, other, total);
}

static void raw_point(int x, int y, const char *label)
{
    printf("%-28s fb0(%d,%d)=0x%04x\n", label, x, y, fb_pixel(x, y));
}

/* destruér alle nuværende børnevinduer på root (vores egne testrester) */
static void kill_children(Display *dpy, Window root)
{
    Window r, parent, *kids = NULL;
    unsigned int n = 0;
    if (XQueryTree(dpy, root, &r, &parent, &kids, &n) && kids) {
        for (unsigned int i = 0; i < n; i++)
            XDestroyWindow(dpy, kids[i]);
        XFree(kids);
    }
    XSync(dpy, False);
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
    printf("aktiv VT: %s (X på vt9)\n", act);
    if (strcmp(act, "tty9") != 0)
        system("chvt 9");

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("/dev/fb0"); return 1; }
    ioctl(fd, FBIOGET_VSCREENINFO, &fb_vi);
    ioctl(fd, FBIOGET_FSCREENINFO, &fb_fi);
    fb = mmap(NULL, (size_t)fb_fi.line_length * fb_vi.yres,
              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
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

    kill_children(dpy, root);
    sleep(1);

    Window win = XCreateSimpleWindow(dpy, root, 100, 80, 480, 270,
                                     1, 0x000000, 0xffff);
    XMapWindow(dpy, win);
    XSync(dpy, False);
    sleep(1);
    region(100, 80, 480, 270, "A efter map (hvid bg?)");
    raw_point(300, 200, "A raw midt");

    XSetForeground(dpy, gc, 0xffff);
    XFillRectangle(dpy, win, gc, 0, 0, 480, 270);
    XSync(dpy, False);
    sleep(1);
    region(100, 80, 480, 270, "A efter fill i vindue");
    XImage *ga = XGetImage(dpy, win, 0, 0, 10, 10, AllPlanes, ZPixmap);
    printf("XGetImage(A) efter fill: 0x%04lx\n", ga ? XGetPixel(ga, 5, 5) : 0);

    XSetForeground(dpy, gc, 0xffff);
    XFillRectangle(dpy, root, gc, 700, 400, 300, 200);
    XSync(dpy, False);
    sleep(1);
    region(700, 400, 300, 200, "B root fill hvid");

    /* C: XPutImage med blå 0xf81f på et frisk root-område */
    unsigned char *cbuf = malloc(640 * 360 * 2);
    for (int i = 0; i < 640 * 360 * 2; i += 2) { cbuf[i] = 0x1f; cbuf[i + 1] = 0xf8; }
    XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0, (char *)cbuf,
                               640, 360, 16, 1280);
    XPutImage(dpy, root, gc, img, 0, 0, 1000, 700, 300, 200);
    XSync(dpy, False);
    sleep(1);
    region(1000, 700, 300, 200, "C root put f81f");
    raw_point(1100, 780, "C raw");
    XImage *gc2 = XGetImage(dpy, root, 1000, 700, 10, 10, AllPlanes, ZPixmap);
    printf("XGetImage(C) efter put: 0x%04lx\n", gc2 ? XGetPixel(gc2, 5, 5) : 0);

    /* D: XPutImage i selve vinduet */
    XPutImage(dpy, win, gc, img, 0, 0, 40, 40, 200, 150);
    XSync(dpy, False);
    sleep(1);
    region(140, 120, 200, 150, "D put f81f i vindue");
    XImage *gd = XGetImage(dpy, win, 40, 40, 10, 10, AllPlanes, ZPixmap);
    printf("XGetImage(D) efter put: 0x%04lx\n", gd ? XGetPixel(gd, 5, 5) : 0);

    printf("X-fejl i alt: %d\n", err_count);

    int out = open("/root/fb_x8.raw", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out >= 0) {
        write(out, fb, (size_t)fb_fi.line_length * fb_vi.yres);
        close(out);
        printf("fb0 dump -> /root/fb_x8.raw\n");
    }

    XDestroyWindow(dpy, win);
    XSync(dpy, False);
    sleep(1);
    return 0;
}
