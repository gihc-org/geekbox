/* egl_proxy.c — tynd proxy for /opt/hybris/libEGL.so.1.0.0.
 *
 * Firefox dlsym'er EGL-funktionerne direkte fra libEGL-handlen (derfor virker
 * LD_PRELOAD ikke). Denne proxy ERSTATTTER hybris-libbet: den eksporterer kun
 * de funktioner vi vil huke (eglCreateContext → log kontekstversion) og linker
 * originalen (omdøbt libEGL_r.so) så alle andre EGL-funktioner resolveres derfra.
 *
 * Byg på boksen:
 *   gcc -shared -fPIC -Wl,-soname,libEGL.so.1 -o /opt/hybris/libEGL.so.1.0.0 \
 *       egl_proxy.c -L/opt/hybris -l:libEGL_r.so -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

static FILE *plog(void)
{
    return fopen("/tmp/fragdepth_probe.log", "a");
}

typedef EGLContext (*real_eglCreateContext_t)(EGLDisplay, EGLConfig, EGLContext,
                                              const EGLint *);

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share,
                            const EGLint *attr)
{
    static real_eglCreateContext_t real;
    if (!real) real = (real_eglCreateContext_t)dlsym(RTLD_NEXT, "eglCreateContext");
    int ver = 0;
    for (const EGLint *a = attr; a && *a != EGL_NONE; a += 2) {
        if (a[0] == EGL_CONTEXT_CLIENT_VERSION) ver = a[1];
    }
    FILE *f = plog();
    if (f) {
        fprintf(f, "eglCreateContext version=%d\n", ver);
        fclose(f);
    }
    return real(dpy, cfg, share, attr);
}
