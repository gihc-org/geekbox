// egl_platform_shim.c — LD_PRELOAD-shim, der eksporterer
// eglGetPlatformDisplayEXT/eglGetPlatformDisplay for Firefox/glxtest.
//
// Baggrund (målt 24. aug 2026): Firefox' EGL-probe (glxtest via libepoxy)
// kræver eglGetPlatformDisplayEXT — vores vendors libEGL (2016, EGL 1.4) har
// den ikke, så proben melder "libEGL no display" og Firefox falder tilbage til
// Mesa-software. Shim'en stiller funktionen til rådighed i global scope (dlsym
// finder LD_PRELOAD-symboler) og viderestiller til den ægte eglGetDisplay —
// som går gennem vores eglplatform_x11 (og gemmer klientens Display*).
//
// Byg på boksen:
//   gcc -O2 -fPIC -shared -o egl_platform_shim.so egl_platform_shim.c \
//       -I/usr/local/include -ldl
// Kør (sammen med system_shim):
//   LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so" \
//   LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 firefox-esr ...
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef EGLDisplay (*eglGetDisplay_t)(EGLNativeDisplayType);
static eglGetDisplay_t real_eglGetDisplay;

static void resolve_egl(void)
{
    void *h = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (h)
        real_eglGetDisplay = (eglGetDisplay_t)dlsym(h, "eglGetDisplay");
    if (!real_eglGetDisplay)
        fprintf(stderr, "[egl-shim] kunne ikke finde ægte eglGetDisplay\n");
}

static void __attribute__((constructor)) egl_shim_init(void)
{
    resolve_egl();
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                    const EGLint *attrib_list)
{
    fprintf(stderr, "[egl-shim] eglGetPlatformDisplayEXT(platform=0x%x "
                    "native=%p)\n", platform, native_display);
    if (!real_eglGetDisplay)
        resolve_egl();
    return real_eglGetDisplay
               ? real_eglGetDisplay((EGLNativeDisplayType)native_display)
               : EGL_NO_DISPLAY;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                 const EGLint *attrib_list)
{
    return eglGetPlatformDisplayEXT(platform, native_display, attrib_list);
}

/* Eksporter også eglGetDisplay selv: i Firefox/glxtest kan symbolopslag ramme
 * Android-loaderens version (fra /system/lib/libEGL.so, loadet via bionic),
 * som fejler med EGL_BAD_DISPLAY (målt: "eglGetDisplay:218 error 300c" i
 * logd). Vores version går gennem hybris-wrapperen → eglplatform_x11. */
EGLDisplay eglGetDisplay(EGLNativeDisplayType native_display)
{
    fprintf(stderr, "[egl-shim] eglGetDisplay(native=%p)\n",
            (void *)native_display);
    if (!real_eglGetDisplay)
        resolve_egl();
    return real_eglGetDisplay
               ? real_eglGetDisplay(native_display)
               : EGL_NO_DISPLAY;
}

/* ---- eglMakeCurrent-eksperiment (26. aug 2026) ----
 * Måling: med gdb-breakpoint på vendors eglMakeCurrent (der får kaldet til
 * at returnere uden at køre) kører Firefox' kompositor 2,7 FPS mod ~1,5 uden.
 * Hypotesen: vendors eglMakeCurrent venter på GPU-en hver frame og er
 * flaskehalsen. Denne wrapper logger kaldet og returnerer EGL_TRUE uden at
 * kalde videre — test-flag EGLSKIP_MAKECURRENT=1 aktiverer. */
typedef EGLBoolean (*eglMakeCurrent_real_t)(EGLDisplay, EGLSurface, EGLSurface,
                                            EGLContext);
static eglMakeCurrent_real_t real_eglMakeCurrent;

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx)
{
    static int skip = -1;
    if (skip < 0)
        skip = getenv("EGLSKIP_MAKECURRENT") ? atoi(getenv("EGLSKIP_MAKECURRENT")) : 0;
    if (skip) {
        fprintf(stderr, "[egl-shim] eglMakeCurrent SKIPPET (dpy=%p draw=%p "
                        "read=%p ctx=%p)\n", (void *)dpy, (void *)draw,
                (void *)read, (void *)ctx);
        return EGL_TRUE;
    }
    if (!real_eglMakeCurrent)
        real_eglMakeCurrent =
            (eglMakeCurrent_real_t)dlsym(RTLD_NEXT, "eglMakeCurrent");
    return real_eglMakeCurrent
               ? real_eglMakeCurrent(dpy, draw, read, ctx)
               : EGL_FALSE;
}
