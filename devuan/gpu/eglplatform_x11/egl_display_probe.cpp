// egl_display_probe.cpp — hvorfor giver eglGetDisplay(NULL) "no display" i
// Firefox/glxtest, mens vores testklient (eglGetDisplay(Display*)) virker?
// Byg på boksen:  g++ -O2 -o egl_display_probe egl_display_probe.cpp \
//                   -I/usr/local/include -L/opt/hybris -lEGL -lX11
// Kør:  LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
//       EGL_PLATFORM=x11 DISPLAY=:0 ./egl_display_probe
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef EGL_PLATFORM_X11_EXT
#define EGL_PLATFORM_X11_EXT 0x31BF
#endif
#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

static void try_display(const char *label, EGLNativeDisplayType native)
{
    EGLDisplay d = eglGetDisplay(native);
    printf("%s: eglGetDisplay=%p fejl=0x%x\n", label, (void *)d, eglGetError());
    if (d != EGL_NO_DISPLAY) {
        EGLint maj = 0, min = 0;
        int ok = eglInitialize(d, &maj, &min);
        printf("   eglInitialize=%d (%d.%d) fejl=0x%x\n", ok, maj, min, eglGetError());
        const char *q = eglQueryString(d, EGL_EXTENSIONS);
        printf("   EGL_EXTENSIONS=%s\n", q ? q : "(null)");
    }
}

int main(void)
{
    setenv("EGL_PLATFORM", "x11", 1);
    const char *client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    printf("CLIENT-EXTENSIONS (EGL_NO_DISPLAY): %s\n", client ? client : "(null)");
    try_display("NULL (EGL_DEFAULT_DISPLAY)", EGL_DEFAULT_DISPLAY);
    Display *x = XOpenDisplay(NULL);
    printf("XOpenDisplay=%p\n", (void *)x);
    if (x) {
        try_display("Display*", (EGLNativeDisplayType)x);
        /* test shim-vejen (eglGetPlatformDisplayEXT) hvis tilgængelig */
        typedef EGLDisplay (*plat_t)(EGLenum, void *, const EGLint *);
        plat_t plat = (plat_t)dlsym(RTLD_DEFAULT, "eglGetPlatformDisplayEXT");
        printf("dlsym(eglGetPlatformDisplayEXT)=%p\n", (void *)plat);
        if (plat) {
            EGLDisplay d = plat(EGL_PLATFORM_X11_EXT, (void *)x, NULL);
            printf("eglGetPlatformDisplayEXT(X11, dpy)=%p fejl=0x%x\n",
                   (void *)d, eglGetError());
        }
    }
    return 0;
}
