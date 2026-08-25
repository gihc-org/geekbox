/* egl_ret_trace.c — LD_PRELOAD-interposer, der logger EGL-kald med
 * ARGUMENTER OG RETURVÆRDIER fra firefox-esr (uden gdb).
 *
 * Byg på boksen:
 *   gcc -O2 -fPIC -shared -o /root/egl_ret_trace.so egl_ret_trace.c \
 *       -I/usr/local/include -ldl -lpthread
 * Kør:
 *   LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so \
 *               /root/egl_ret_trace.so" LD_LIBRARY_PATH=/opt/hybris \
 *   EGL_PLATFORM=x11 DISPLAY=:0 firefox-esr ...
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef EGLContext (*create_t)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean (*makecur_t)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLint (*geterr_t)(void);
typedef EGLBoolean (*bindapi_t)(EGLenum);
typedef EGLBoolean (*choose_t)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef __eglMustCastToProperFunctionPointerType (*getproc_t)(const char*);
typedef EGLSurface (*createpb_t)(EGLDisplay, EGLConfig, const EGLint*);
typedef EGLBoolean (*destroy_t)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*destroyctx_t)(EGLDisplay, EGLContext);

static long thrid(void) { return syscall(SYS_gettid); }

static void* next(const char* n) {
    void* p = dlsym(RTLD_NEXT, n);
    if (!p) fprintf(stderr, "EGLTRC: dlsym(RTLD_NEXT,%s) FEJL: %s\n", n, dlerror());
    return p;
}

static void dump_attrs(const EGLint* a) {
    if (!a) { fprintf(stderr, "  attrs=NULL\n"); return; }
    for (int i = 0; i < 64 && a[i] != 0x3038; i += 2)
        fprintf(stderr, "  a[0x%x]=0x%x\n", (unsigned)a[i], (unsigned)a[i+1]);
}

EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext s, const EGLint* a) {
    static create_t real;
    if (!real) real = (create_t)next("eglCreateContext");
    EGLContext r = real(d, c, s, a);
    fprintf(stderr, "EGLTRC[t%ld] eglCreateContext dpy=%p cfg=%p share=%p => %p caller=%p\n",
            thrid(), (void*)d, (void*)c, (void*)s, (void*)r,
            __builtin_return_address(0));
    dump_attrs(a);
    fflush(stderr);
    return r;
}

EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read,
                          EGLContext ctx) {
    static makecur_t real;
    if (!real) real = (makecur_t)next("eglMakeCurrent");
    EGLBoolean r = real(d, draw, read, ctx);
    fprintf(stderr, "EGLTRC[t%ld] eglMakeCurrent dpy=%p draw=%p read=%p ctx=%p => %d\n",
            thrid(), (void*)d, (void*)draw, (void*)read, (void*)ctx, (int)r);
    fflush(stderr);
    return r;
}

EGLint eglGetError(void) {
    static geterr_t real;
    if (!real) real = (geterr_t)next("eglGetError");
    EGLint r = real();
    fprintf(stderr, "EGLTRC[t%ld] eglGetError => 0x%x\n", thrid(), (unsigned)r);
    fflush(stderr);
    return r;
}

EGLBoolean eglBindAPI(EGLenum api) {
    static bindapi_t real;
    if (!real) real = (bindapi_t)next("eglBindAPI");
    EGLBoolean r = real(api);
    fprintf(stderr, "EGLTRC[t%ld] eglBindAPI api=0x%x => %d\n",
            thrid(), (unsigned)api, (int)r);
    fflush(stderr);
    return r;
}

EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint* a, EGLConfig* cfgs,
                           EGLint n, EGLint* out) {
    static choose_t real;
    if (!real) real = (choose_t)next("eglChooseConfig");
    EGLBoolean r = real(d, a, cfgs, n, out);
    fprintf(stderr, "EGLTRC[t%ld] eglChooseConfig dpy=%p => %d n=%d first=%p\n",
            thrid(), (void*)d, (int)r, out ? (int)*out : -1,
            (cfgs && out && *out > 0) ? (void*)cfgs[0] : NULL);
    fflush(stderr);
    return r;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* name) {
    static getproc_t real;
    if (!real) real = (getproc_t)next("eglGetProcAddress");
    __eglMustCastToProperFunctionPointerType r = real(name);
    if (name && (name[0] == 'g' || name[0] == 'e'))
        fprintf(stderr, "EGLTRC[t%ld] eglGetProcAddress(%s) => %p\n",
                thrid(), name, (void*)r);
    fflush(stderr);
    return r;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay d, EGLConfig c, const EGLint* a) {
    static createpb_t real;
    if (!real) real = (createpb_t)next("eglCreatePbufferSurface");
    EGLSurface r = real(d, c, a);
    fprintf(stderr, "EGLTRC[t%ld] eglCreatePbufferSurface dpy=%p cfg=%p => %p caller=%p\n",
            thrid(), (void*)d, (void*)c, (void*)r, __builtin_return_address(0));
    if (a) for (int i = 0; i < 32 && a[i] != 0x3038; i += 2)
        fprintf(stderr, "  a[0x%x]=0x%x\n", (unsigned)a[i], (unsigned)a[i+1]);
    fflush(stderr);
    return r;
}

EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s) {
    static destroy_t real;
    if (!real) real = (destroy_t)next("eglDestroySurface");
    EGLBoolean r = real(d, s);
    fprintf(stderr, "EGLTRC[t%ld] eglDestroySurface dpy=%p surf=%p => %d\n",
            thrid(), (void*)d, (void*)s, (int)r);
    fflush(stderr);
    return r;
}

EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext c) {
    static destroyctx_t real;
    if (!real) real = (destroyctx_t)next("eglDestroyContext");
    EGLBoolean r = real(d, c);
    fprintf(stderr, "EGLTRC[t%ld] eglDestroyContext dpy=%p ctx=%p => %d\n",
            thrid(), (void*)d, (void*)c, (int)r);
    fflush(stderr);
    return r;
}
