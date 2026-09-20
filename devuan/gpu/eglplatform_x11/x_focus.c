/* x_focus.c - saet X-inputfokus paa Firefox-vinduet (og bed WM'en aktivere det).
 *
 * Baggrund (17. sep 2026): i vores automatiske maalekoersler blev Firefox
 * startet fra ssh, og vinduet fik aldrig fokus (document.hasFocus() = false).
 * Browsere begraenser rAF/opdatering for vinduer de mener ikke er i forgrunden,
 * saa maalt 3-20 rAF/s paa en triviel side. Dette vaerktoej giver vinduet fokus
 * fra X-siden, saa maalingerne svarer til en bruger der sidder foran skaermen.
 *
 * Byg/koer paa boksen:
 *   gcc -O2 -o /root/x_focus /root/x_focus.c -lX11
 *   DISPLAY=:0 /root/x_focus [-big] [wmklasse-substreng]
 *
 * 20. sep 2026: to fejl fundet i de hidtidige maalinger:
 *   (1) scriptet koerte værktøjet via "runuser -u kristian", men /root er 0700,
 *       saa det fejlede med "env: '/root/x_focus': Permission denied" — fokus
 *       blev ALDRIG sat (det var ikke Xauthority, som notatet antog). X tager
 *       imod root uden .Xauthority, saa koer det som root i stedet.
 *   (2) uden -big tages det foerste vindue hvis klasse indeholder "firefox" —
 *       Firefox' hjaelpevinduer (200x200) ligger foer hovedvinduet. Brug -big.
 */
#define _GNU_SOURCE
#include <X11/Xatom.h>
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
        if (strstr(name, needle)) {
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

/* Stoerste vindue hvis navn/klasse matcher (hovedvinduet, ikke hjaelpevinduerne). */
static Window find_biggest(Display *d, Window w, const char *needle, int depth,
                           unsigned long *best)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0;
    Window found = 0;
    int hit = 0;
    char *name = NULL;
    XWindowAttributes at;

    if (XFetchName(d, w, &name) && name) {
        if (strstr(name, needle))
            hit = 1;
        XFree(name);
    }
    if (!hit) {
        XClassHint ch;
        memset(&ch, 0, sizeof ch);
        if (XGetClassHint(d, w, &ch)) {
            if (ch.res_name && strcasestr(ch.res_name, needle))
                hit = 1;
            if (ch.res_class && strcasestr(ch.res_class, needle))
                hit = 1;
            if (ch.res_name)
                XFree(ch.res_name);
            if (ch.res_class)
                XFree(ch.res_class);
        }
    }
    if (hit && XGetWindowAttributes(d, w, &at) && !at.override_redirect) {
        unsigned long area = (unsigned long)at.width * (unsigned long)at.height;
        if (area > *best) {
            *best = area;
            found = w;
        }
    }
    if (depth <= 6 && XQueryTree(d, w, &root, &parent, &kids, &n) && kids) {
        for (unsigned i = 0; i < n; i++) {
            Window c = find_biggest(d, kids[i], needle, depth + 1, best);
            if (c)
                found = c;
        }
        XFree(kids);
    }
    return found;
}

int main(int argc, char **argv)
{
    int big = 0;
    int a = 1;

    if (argc > 1 && strcmp(argv[1], "-big") == 0) {
        big = 1;
        a = 2;
    }
    const char *needle = argc > a ? argv[a] : "firefox";
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "kan ikke aabne DISPLAY\n");
        return 1;
    }
    Window root = DefaultRootWindow(d);
    Window w;
    if (big) {
        unsigned long best = 0;
        w = find_biggest(d, root, needle, 0, &best);
    } else {
        w = find_window(d, root, needle, 0);
    }
    if (!w) {
        fprintf(stderr, "fandt intet vindue med '%s'\n", needle);
        return 2;
    }
    XRaiseWindow(d, w);
    /* Bed foerst WM'en (openbox) om at aktivere vinduet, og saet derefter
     * inputfokus direkte — openbox kan naa at flytte fokus efter XRaise. */
    Atom net_active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = net_active;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(d);
    XSync(d, False);
    XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XFlush(d);
    XSync(d, False);
    Window focused = None;
    int revert = 0;
    XGetInputFocus(d, &focused, &revert);
    XWindowAttributes at;
    XGetWindowAttributes(d, w, &at);
    printf("fokus sat paa vindue 0x%lx (%dx%d, soegte '%s'); X siger fokus=0x%lx\n",
           (unsigned long)w, at.width, at.height, needle, (unsigned long)focused);
    XCloseDisplay(d);
    return 0;
}
