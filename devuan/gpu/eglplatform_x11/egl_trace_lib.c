/* egl_trace_lib.c — fuld libEGL.so.1-erstatning med logning.
 *
 * Lægges i /root/egl_trace/ (FØRST i LD_LIBRARY_PATH), så firefox-esr's
 * dlopen("libEGL.so.1") rammer OS, der logger alle EGL-kald (argumenter +
 * returværdier) og videresender til den ægte wrapper
 * /opt/hybris/libEGL.so.1 (loadet via absolut sti for at undgå selv-referens).
 *
 * Byg på boksen:
 *   mkdir -p /root/egl_trace
 *   gcc -O2 -fPIC -shared -o /root/egl_trace/libEGL.so.1 egl_trace_lib.c \
 *       -I/usr/local/include -ldl -lpthread
 * Kør:
 *   LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so" \
 *   LD_LIBRARY_PATH=/root/egl_trace:/opt/hybris EGL_PLATFORM=x11 \
 *   DISPLAY=:0 firefox-esr ...
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#define REAL_PATH "/opt/hybris/libEGL.so.1"

static void* real_h = NULL;
static int log_on = 1;

static long thrid(void) { return syscall(SYS_gettid); }

static void* real_sym(const char* n) {
    if (!real_h) {
        real_h = dlopen(REAL_PATH, RTLD_NOW | RTLD_LOCAL);
        if (!real_h) {
            fprintf(stderr, "EGLSHIM: dlopen(%s) FEJL: %s\n",
                    REAL_PATH, dlerror());
            return NULL;
        }
    }
    void* p = dlsym(real_h, n);
    if (!p) fprintf(stderr, "EGLSHIM: dlsym(%s) FEJL: %s\n", n, dlerror());
    return p;
}

typedef void* (*gp_t)(const char*);

#define TYPEDEFS_AND_FORWARDS \
    typedef EGLDisplay (*f_eglGetDisplay)(EGLNativeDisplayType); \
    typedef EGLBoolean (*f_eglInitialize)(EGLDisplay, EGLint*, EGLint*); \
    typedef EGLBoolean (*f_eglTerminate)(EGLDisplay); \
    typedef EGLBoolean (*f_eglBindAPI)(EGLenum); \
    typedef EGLenum (*f_eglQueryAPI)(void); \
    typedef EGLBoolean (*f_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*); \
    typedef EGLBoolean (*f_eglGetConfigs)(EGLDisplay, EGLConfig*, EGLint, EGLint*); \
    typedef EGLBoolean (*f_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint*); \
    typedef EGLSurface (*f_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*); \
    typedef EGLSurface (*f_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*); \
    typedef EGLSurface (*f_eglCreatePixmapSurface)(EGLDisplay, EGLConfig, EGLNativePixmapType, const EGLint*); \
    typedef EGLSurface (*f_eglCreatePbufferFromClientBuffer)(EGLDisplay, EGLenum, EGLClientBuffer, EGLConfig, const EGLint*); \
    typedef EGLBoolean (*f_eglDestroySurface)(EGLDisplay, EGLSurface); \
    typedef EGLBoolean (*f_eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint*); \
    typedef EGLContext (*f_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*); \
    typedef EGLBoolean (*f_eglDestroyContext)(EGLDisplay, EGLContext); \
    typedef EGLBoolean (*f_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext); \
    typedef EGLContext (*f_eglGetCurrentContext)(void); \
    typedef EGLSurface (*f_eglGetCurrentSurface)(EGLint); \
    typedef EGLDisplay (*f_eglGetCurrentDisplay)(void); \
    typedef EGLBoolean (*f_eglQueryContext)(EGLDisplay, EGLContext, EGLint, EGLint*); \
    typedef EGLBoolean (*f_eglWaitGL)(void); \
    typedef EGLBoolean (*f_eglWaitClient)(void); \
    typedef EGLBoolean (*f_eglReleaseThread)(void); \
    typedef EGLBoolean (*f_eglWaitNative)(EGLint); \
    typedef EGLBoolean (*f_eglSwapBuffers)(EGLDisplay, EGLSurface); \
    typedef EGLBoolean (*f_eglCopyBuffers)(EGLDisplay, EGLSurface, EGLNativePixmapType); \
    typedef EGLBoolean (*f_eglSwapInterval)(EGLDisplay, EGLint); \
    typedef EGLBoolean (*f_eglSurfaceAttrib)(EGLDisplay, EGLSurface, EGLint, EGLint); \
    typedef EGLBoolean (*f_eglBindTexImage)(EGLDisplay, EGLSurface, EGLint); \
    typedef EGLBoolean (*f_eglReleaseTexImage)(EGLDisplay, EGLSurface, EGLint); \
    typedef const char* (*f_eglQueryString)(EGLDisplay, EGLint); \
    typedef EGLint (*f_eglGetError)(void); \
    typedef __eglMustCastToProperFunctionPointerType (*f_eglGetProcAddress)(const char*); \
    typedef EGLBoolean (*f_eglDestroyImageKHR)(EGLDisplay, EGLImageKHR)

TYPEDEFS_AND_FORWARDS;

static void dump_attrs(const EGLint* a) {
    if (!a) { fprintf(stderr, "  attrs=NULL\n"); return; }
    for (int i = 0; i < 64 && a[i] != 0x3038; i += 2)
        fprintf(stderr, "  a[0x%x]=0x%x\n", (unsigned)a[i], (unsigned)a[i+1]);
}

/* ---- loggende funktioner ---- */

EGLDisplay eglGetDisplay(EGLNativeDisplayType native) {
    static f_eglGetDisplay r;
    if (!r) r = (f_eglGetDisplay)real_sym("eglGetDisplay");
    EGLDisplay v = r(native);
    fprintf(stderr, "EGLSHIM[t%ld] eglGetDisplay(native=%p) => %p\n",
            thrid(), (void*)native, (void*)v);
    return v;
}

EGLBoolean eglInitialize(EGLDisplay d, EGLint* maj, EGLint* min) {
    static f_eglInitialize r;
    if (!r) r = (f_eglInitialize)real_sym("eglInitialize");
    EGLBoolean v = r(d, maj, min);
    fprintf(stderr, "EGLSHIM[t%ld] eglInitialize(dpy=%p) => %d maj=%d min=%d\n",
            thrid(), (void*)d, (int)v, maj ? *maj : -1, min ? *min : -1);
    return v;
}

EGLBoolean eglTerminate(EGLDisplay d) {
    static f_eglTerminate r;
    if (!r) r = (f_eglTerminate)real_sym("eglTerminate");
    EGLBoolean v = r(d);
    fprintf(stderr, "EGLSHIM[t%ld] eglTerminate(dpy=%p) => %d\n",
            thrid(), (void*)d, (int)v);
    return v;
}

EGLBoolean eglBindAPI(EGLenum api) {
    static f_eglBindAPI r;
    if (!r) r = (f_eglBindAPI)real_sym("eglBindAPI");
    EGLBoolean v = r(api);
    fprintf(stderr, "EGLSHIM[t%ld] eglBindAPI(0x%x) => %d caller=%p\n",
            thrid(), (unsigned)api, (int)v, __builtin_return_address(0));
    return v;
}

EGLenum eglQueryAPI(void) {
    static f_eglQueryAPI r;
    if (!r) r = (f_eglQueryAPI)real_sym("eglQueryAPI");
    EGLenum v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglQueryAPI() => 0x%x\n", thrid(), (unsigned)v);
    return v;
}

EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint* a, EGLConfig* cfgs,
                           EGLint n, EGLint* out) {
    static f_eglChooseConfig r;
    if (!r) r = (f_eglChooseConfig)real_sym("eglChooseConfig");
    EGLBoolean v = r(d, a, cfgs, n, out);
    fprintf(stderr, "EGLSHIM[t%ld] eglChooseConfig(dpy=%p) => %d n=%d first=%p\n",
            thrid(), (void*)d, (int)v, out ? *out : -1,
            (cfgs && out && *out > 0) ? (void*)cfgs[0] : NULL);
    if (log_on) dump_attrs(a);
    return v;
}

EGLBoolean eglGetConfigs(EGLDisplay d, EGLConfig* cfgs, EGLint n, EGLint* out) {
    static f_eglGetConfigs r;
    if (!r) r = (f_eglGetConfigs)real_sym("eglGetConfigs");
    EGLBoolean v = r(d, cfgs, n, out);
    fprintf(stderr, "EGLSHIM[t%ld] eglGetConfigs(dpy=%p) => %d n=%d\n",
            thrid(), (void*)d, (int)v, out ? *out : -1);
    return v;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay d, EGLConfig c, EGLint name, EGLint* val) {
    static f_eglGetConfigAttrib r;
    if (!r) r = (f_eglGetConfigAttrib)real_sym("eglGetConfigAttrib");
    EGLBoolean v = r(d, c, name, val);
    fprintf(stderr, "EGLSHIM[t%ld] eglGetConfigAttrib(cfg=%p name=0x%x) => %d val=%d\n",
            thrid(), (void*)c, (unsigned)name, (int)v, val ? *val : -1);
    return v;
}

EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig c,
                                  EGLNativeWindowType w, const EGLint* a) {
    static f_eglCreateWindowSurface r;
    if (!r) r = (f_eglCreateWindowSurface)real_sym("eglCreateWindowSurface");
    EGLSurface v = r(d, c, w, a);
    fprintf(stderr, "EGLSHIM[t%ld] eglCreateWindowSurface(dpy=%p cfg=%p win=%p) => %p\n",
            thrid(), (void*)d, (void*)c, (void*)w, (void*)v);
    return v;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay d, EGLConfig c, const EGLint* a) {
    static f_eglCreatePbufferSurface r;
    if (!r) r = (f_eglCreatePbufferSurface)real_sym("eglCreatePbufferSurface");
    EGLSurface v = r(d, c, a);
    fprintf(stderr, "EGLSHIM[t%ld] eglCreatePbufferSurface(dpy=%p cfg=%p) => %p caller=%p\n",
            thrid(), (void*)d, (void*)c, (void*)v, __builtin_return_address(0));
    if (log_on) dump_attrs(a);
    return v;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay d, EGLConfig c,
                                  EGLNativePixmapType p, const EGLint* a) {
    static f_eglCreatePixmapSurface r;
    if (!r) r = (f_eglCreatePixmapSurface)real_sym("eglCreatePixmapSurface");
    EGLSurface v = r(d, c, p, a);
    fprintf(stderr, "EGLSHIM[t%ld] eglCreatePixmapSurface(dpy=%p cfg=%p) => %p\n",
            thrid(), (void*)d, (void*)c, (void*)v);
    return v;
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay d, EGLenum buftype,
                                            EGLClientBuffer buf, EGLConfig c,
                                            const EGLint* a) {
    static f_eglCreatePbufferFromClientBuffer r;
    if (!r) r = (f_eglCreatePbufferFromClientBuffer)
                    real_sym("eglCreatePbufferFromClientBuffer");
    EGLSurface v = r(d, buftype, buf, c, a);
    fprintf(stderr, "EGLSHIM[t%ld] eglCreatePbufferFromClientBuffer => %p\n",
            thrid(), (void*)v);
    return v;
}

EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s) {
    static f_eglDestroySurface r;
    if (!r) r = (f_eglDestroySurface)real_sym("eglDestroySurface");
    EGLBoolean v = r(d, s);
    fprintf(stderr, "EGLSHIM[t%ld] eglDestroySurface(dpy=%p surf=%p) => %d\n",
            thrid(), (void*)d, (void*)s, (int)v);
    return v;
}

EGLBoolean eglQuerySurface(EGLDisplay d, EGLSurface s, EGLint name, EGLint* val) {
    static f_eglQuerySurface r;
    if (!r) r = (f_eglQuerySurface)real_sym("eglQuerySurface");
    EGLBoolean v = r(d, s, name, val);
    fprintf(stderr, "EGLSHIM[t%ld] eglQuerySurface(surf=%p name=0x%x) => %d val=%d\n",
            thrid(), (void*)s, (unsigned)name, (int)v, val ? *val : -1);
    return v;
}

EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share,
                            const EGLint* a) {
    static f_eglCreateContext r;
    if (!r) r = (f_eglCreateContext)real_sym("eglCreateContext");
    EGLContext v = r(d, c, share, a);
    fprintf(stderr, "EGLSHIM[t%ld] eglCreateContext(dpy=%p cfg=%p share=%p) => %p caller=%p\n",
            thrid(), (void*)d, (void*)c, (void*)share, (void*)v,
            __builtin_return_address(0));
    if (log_on) dump_attrs(a);
    return v;
}

EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext c) {
    static f_eglDestroyContext r;
    if (!r) r = (f_eglDestroyContext)real_sym("eglDestroyContext");
    EGLBoolean v = r(d, c);
    fprintf(stderr, "EGLSHIM[t%ld] eglDestroyContext(dpy=%p ctx=%p) => %d\n",
            thrid(), (void*)d, (void*)c, (int)v);
    return v;
}

EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read,
                          EGLContext c) {
    static f_eglMakeCurrent r;
    if (!r) r = (f_eglMakeCurrent)real_sym("eglMakeCurrent");
    EGLBoolean v = r(d, draw, read, c);
    fprintf(stderr, "EGLSHIM[t%ld] eglMakeCurrent(dpy=%p draw=%p read=%p ctx=%p) => %d caller=%p\n",
            thrid(), (void*)d, (void*)draw, (void*)read, (void*)c, (int)v,
            __builtin_return_address(0));
    return v;
}

EGLContext eglGetCurrentContext(void) {
    static f_eglGetCurrentContext r;
    if (!r) r = (f_eglGetCurrentContext)real_sym("eglGetCurrentContext");
    EGLContext v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglGetCurrentContext() => %p\n",
            thrid(), (void*)v);
    return v;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    static f_eglGetCurrentSurface r;
    if (!r) r = (f_eglGetCurrentSurface)real_sym("eglGetCurrentSurface");
    EGLSurface v = r(readdraw);
    fprintf(stderr, "EGLSHIM[t%ld] eglGetCurrentSurface(0x%x) => %p\n",
            thrid(), (unsigned)readdraw, (void*)v);
    return v;
}

EGLDisplay eglGetCurrentDisplay(void) {
    static f_eglGetCurrentDisplay r;
    if (!r) r = (f_eglGetCurrentDisplay)real_sym("eglGetCurrentDisplay");
    EGLDisplay v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglGetCurrentDisplay() => %p\n",
            thrid(), (void*)v);
    return v;
}

EGLBoolean eglQueryContext(EGLDisplay d, EGLContext c, EGLint name, EGLint* val) {
    static f_eglQueryContext r;
    if (!r) r = (f_eglQueryContext)real_sym("eglQueryContext");
    EGLBoolean v = r(d, c, name, val);
    fprintf(stderr, "EGLSHIM[t%ld] eglQueryContext(ctx=%p name=0x%x) => %d val=%d\n",
            thrid(), (void*)c, (unsigned)name, (int)v, val ? *val : -1);
    return v;
}

EGLBoolean eglWaitGL(void) {
    static f_eglWaitGL r;
    if (!r) r = (f_eglWaitGL)real_sym("eglWaitGL");
    EGLBoolean v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglWaitGL() => %d\n", thrid(), (int)v);
    return v;
}

EGLBoolean eglWaitClient(void) {
    static f_eglWaitClient r;
    if (!r) r = (f_eglWaitClient)real_sym("eglWaitClient");
    EGLBoolean v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglWaitClient() => %d\n", thrid(), (int)v);
    return v;
}

EGLBoolean eglReleaseThread(void) {
    static f_eglReleaseThread r;
    if (!r) r = (f_eglReleaseThread)real_sym("eglReleaseThread");
    EGLBoolean v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglReleaseThread() => %d\n", thrid(), (int)v);
    return v;
}

EGLBoolean eglWaitNative(EGLint engine) {
    static f_eglWaitNative r;
    if (!r) r = (f_eglWaitNative)real_sym("eglWaitNative");
    EGLBoolean v = r(engine);
    fprintf(stderr, "EGLSHIM[t%ld] eglWaitNative(0x%x) => %d\n",
            thrid(), (unsigned)engine, (int)v);
    return v;
}

EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface s) {
    static f_eglSwapBuffers r;
    if (!r) r = (f_eglSwapBuffers)real_sym("eglSwapBuffers");
    EGLBoolean v = r(d, s);
    fprintf(stderr, "EGLSHIM[t%ld] eglSwapBuffers(dpy=%p surf=%p) => %d\n",
            thrid(), (void*)d, (void*)s, (int)v);
    return v;
}

EGLBoolean eglCopyBuffers(EGLDisplay d, EGLSurface s, EGLNativePixmapType t) {
    static f_eglCopyBuffers r;
    if (!r) r = (f_eglCopyBuffers)real_sym("eglCopyBuffers");
    EGLBoolean v = r(d, s, t);
    fprintf(stderr, "EGLSHIM[t%ld] eglCopyBuffers => %d\n", thrid(), (int)v);
    return v;
}

EGLBoolean eglSwapInterval(EGLDisplay d, EGLint interval) {
    static f_eglSwapInterval r;
    if (!r) r = (f_eglSwapInterval)real_sym("eglSwapInterval");
    EGLBoolean v = r(d, interval);
    fprintf(stderr, "EGLSHIM[t%ld] eglSwapInterval(dpy=%p i=%d) => %d\n",
            thrid(), (void*)d, (int)interval, (int)v);
    return v;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay d, EGLSurface s, EGLint name, EGLint val) {
    static f_eglSurfaceAttrib r;
    if (!r) r = (f_eglSurfaceAttrib)real_sym("eglSurfaceAttrib");
    EGLBoolean v = r(d, s, name, val);
    fprintf(stderr, "EGLSHIM[t%ld] eglSurfaceAttrib(surf=%p name=0x%x val=%d) => %d\n",
            thrid(), (void*)s, (unsigned)name, (int)val, (int)v);
    return v;
}

EGLBoolean eglBindTexImage(EGLDisplay d, EGLSurface s, EGLint buf) {
    static f_eglBindTexImage r;
    if (!r) r = (f_eglBindTexImage)real_sym("eglBindTexImage");
    EGLBoolean v = r(d, s, buf);
    fprintf(stderr, "EGLSHIM[t%ld] eglBindTexImage => %d\n", thrid(), (int)v);
    return v;
}

EGLBoolean eglReleaseTexImage(EGLDisplay d, EGLSurface s, EGLint buf) {
    static f_eglReleaseTexImage r;
    if (!r) r = (f_eglReleaseTexImage)real_sym("eglReleaseTexImage");
    EGLBoolean v = r(d, s, buf);
    fprintf(stderr, "EGLSHIM[t%ld] eglReleaseTexImage => %d\n", thrid(), (int)v);
    return v;
}

const char* eglQueryString(EGLDisplay d, EGLint name) {
    static f_eglQueryString r;
    if (!r) r = (f_eglQueryString)real_sym("eglQueryString");
    const char* v = r(d, name);
    fprintf(stderr, "EGLSHIM[t%ld] eglQueryString(dpy=%p name=0x%x) => %s\n",
            thrid(), (void*)d, (unsigned)name, v ? v : "(null)");
    return v;
}

EGLint eglGetError(void) {
    static f_eglGetError r;
    if (!r) r = (f_eglGetError)real_sym("eglGetError");
    EGLint v = r();
    fprintf(stderr, "EGLSHIM[t%ld] eglGetError() => 0x%x\n", thrid(), (unsigned)v);
    return v;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* name) {
    static f_eglGetProcAddress r;
    if (!r) r = (f_eglGetProcAddress)real_sym("eglGetProcAddress");
    __eglMustCastToProperFunctionPointerType v = r(name);
    if (name && (name[0] == 'g' || name[0] == 'e'))
        fprintf(stderr, "EGLSHIM[t%ld] eglGetProcAddress(%s) => %p\n",
                thrid(), name, (void*)v);
    return v;
}

EGLBoolean eglDestroyImageKHR(EGLDisplay d, EGLImageKHR img) {
    static f_eglDestroyImageKHR r;
    if (!r) r = (f_eglDestroyImageKHR)real_sym("eglDestroyImageKHR");
    EGLBoolean v = r(d, img);
    fprintf(stderr, "EGLSHIM[t%ld] eglDestroyImageKHR(dpy=%p img=%p) => %d\n",
            thrid(), (void*)d, (void*)img, (int)v);
    return v;
}
