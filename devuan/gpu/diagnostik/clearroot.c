/* clearroot.c — fyld hele root-vinduet sort (rydder gamle fb0-rester).
 *
 * Baggrund (M2b): X' fbdev-driver maler IKKE root-baggrund ved opstart, så
 * gamle direkte-fb0-blits (kiosk-UI, testrektangler) bliver stående på
 * skærmen. Denne helper giver en ren tavle før demo-tests.
 *
 * Byg/afvikling på boksen:
 *   gcc -O2 -o /tmp/clearroot /tmp/clearroot.c -lX11
 *   DISPLAY=:0 /tmp/clearroot
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    Display *dpy = XOpenDisplay(":0");
    if (!dpy) { fprintf(stderr, "kan ikke åbne :0\n"); return 1; }
    int scr = DefaultScreen(dpy);
    GC gc = DefaultGC(dpy, scr);
    XSetForeground(dpy, gc, 0x0000);
    XFillRectangle(dpy, RootWindow(dpy, scr), gc, 0, 0,
                   DisplayWidth(dpy, scr), DisplayHeight(dpy, scr));
    XSync(dpy, False);
    sleep(1);
    XCloseDisplay(dpy);
    return 0;
}
