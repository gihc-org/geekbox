/*
 * epoxy_mimic.c — tester libepoxy's EGL-dispatch, præcis som Firefox/libxul
 * linker mod den.
 *
 * Baggrund (FIREFOX-WEBCL-SESSION-NOTAT-2026-08-24.md §9.1):
 *   Firefox' WebRender-hardwarekontekst fejler med 0x300c (EGL_BAD_DISPLAY)
 *   i eglCreateContext UDEN at ramme hybris-wrapperens eglCreateContext
 *   (mønster A). Mistanken er libepoxy: libepoxy.so.0 eksporterer sine egne
 *   egl*-funktioner, som løser de rigtige pointere via
 *   eglGetProcAddress(name) med fallback til dlsym(libegl, name).
 *
 * Mimicken gør:
 *   1. dlopen("libepoxy.so.0") og dlsym'er epoxy's EGNE egl*-eksporter.
 *   2. Rekonstruerer libepoxy's interne opslagskæde (gpa -> dlsym) og
 *      sammenligner pointerne med epoxy's eksporter.
 *   3. Kalder hele EGL-dansen IGENNEM epoxy's eksporter (display, init,
 *      config, bindAPI, createContext, makeCurrent, glGetString) og logger
 *      fejl efter hvert kald — både ES-vejen (mønster A) og desktop-GL-vejen
 *      (mønster B, robustness-attributter).
 *
 * Byg på boksen (se build_epoxy_mimic.sh):
 *   gcc -O0 -g -o epoxy_mimic epoxy_mimic.c \
 *       -I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm
 *
 * Kør med samme env som Firefox:
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *   EGL_PLATFORM=x11 DISPLAY=:0 ./epoxy_mimic
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30fb
#endif
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30fd
#endif
#ifndef EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT 0x31bd
#endif
#ifndef EGL_LOSE_CONTEXT_ON_RESET_EXT
#define EGL_LOSE_CONTEXT_ON_RESET_EXT 0x31bf
#endif
#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR 0x31bd
#endif
#ifndef EGL_LOSE_CONTEXT_ON_RESET_KHR
#define EGL_LOSE_CONTEXT_ON_RESET_KHR 0x31bf
#endif
#ifndef EGL_CONTEXT_FLAGS_KHR
#define EGL_CONTEXT_FLAGS_KHR 0x30fc
#endif
#ifndef EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR 0x00000004
#endif
#ifndef EGL_PLATFORM_X11_EXT
#define EGL_PLATFORM_X11_EXT 0x31d5
#endif
#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif

/* Alle de navne Firefox/libepoxy kan bede om i EGL-stien. */
static const char* kNames[] = {
    "eglGetDisplay",         "eglInitialize",       "eglTerminate",
    "eglChooseConfig",       "eglGetConfigAttrib",  "eglGetConfigs",
    "eglBindAPI",            "eglCreateContext",    "eglDestroyContext",
    "eglMakeCurrent",        "eglGetCurrentSurface","eglGetCurrentContext",
    "eglCreateWindowSurface","eglCreatePbufferSurface",
    "eglDestroySurface",     "eglSwapBuffers",      "eglGetError",
    "eglGetProcAddress",     "eglQueryString",      "eglQuerySurface",
    "eglSwapInterval",       "eglWaitNative",       "eglCopyBuffers",
    "eglQueryContext",       "eglBindTexImage",     "eglReleaseTexImage",
    "eglGetPlatformDisplayEXT", "eglGetPlatformDisplay",
    NULL};

typedef EGLDisplay (*pfn_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*pfn_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*pfn_eglTerminate)(EGLDisplay);
typedef EGLBoolean (*pfn_eglChooseConfig)(EGLDisplay, const EGLint*,
                                          EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*pfn_eglGetConfigAttrib)(EGLDisplay, EGLConfig,
                                             EGLint, EGLint*);
typedef EGLBoolean (*pfn_eglBindAPI)(EGLenum);
typedef EGLContext (*pfn_eglCreateContext)(EGLDisplay, EGLConfig,
                                           EGLContext, const EGLint*);
typedef EGLBoolean (*pfn_eglDestroyContext)(EGLDisplay, EGLContext);
typedef EGLBoolean (*pfn_eglMakeCurrent)(EGLDisplay, EGLSurface,
                                         EGLSurface, EGLContext);
typedef EGLSurface (*pfn_eglCreatePbufferSurface)(EGLDisplay, EGLConfig,
                                                  const EGLint*);
typedef EGLBoolean (*pfn_eglDestroySurface)(EGLDisplay, EGLSurface);
typedef EGLint (*pfn_eglGetError)(void);
typedef EGLBoolean (*pfn_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef void* (*pfn_eglGetProcAddress)(const char*);
typedef const char* (*pfn_eglQueryString)(EGLDisplay, EGLint);
typedef const unsigned char* (*pfn_glGetString)(unsigned int);

static void* g_libegl = NULL;          /* wrapper /opt/hybris/libEGL.so.1 */
static void* g_libepoxy = NULL;        /* libepoxy.so.0 */
static pfn_eglGetProcAddress g_gpa = NULL; /* wrapperens eglGetProcAddress */

static const char* hexerr(EGLint e)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "0x%x", (unsigned)e);
    return buf;
}

static void print_maps_egl(void)
{
    FILE* m = fopen("/proc/self/maps", "r");
    char line[512];
    printf("== maps (EGL/GLES/epoxy/hybris/system) ==\n");
    while (m && fgets(line, sizeof(line), m)) {
        if (strstr(line, "libEGL") || strstr(line, "libGLES") ||
            strstr(line, "epoxy") || strstr(line, "hybris") ||
            strstr(line, "eglplatform_x11") || strstr(line, "system/lib"))
            fputs(line, stdout);
    }
    if (m) fclose(m);
}

/* Trin 1: hvad eksporterer libepoxy.so.0 selv? */
static void step_epoxy_exports(void)
{
    printf("\n== 1. libepoxy's egne egl*-eksporter ==\n");
    g_libepoxy = dlopen("libepoxy.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!g_libepoxy) {
        printf("dlopen(libepoxy.so.0) FAIL: %s\n", dlerror());
        return;
    }
    printf("dlopen(libepoxy.so.0)=%p\n", g_libepoxy);
    for (int i = 0; kNames[i]; i++) {
        void* p = dlsym(g_libepoxy, kNames[i]);
        void* def = dlsym(RTLD_DEFAULT, kNames[i]);
        printf("  epoxy[%-32s]=%p   dlsym(RTLD_DEFAULT)=%p%s\n",
               kNames[i], p, def,
               (p && p == def) ? "  (samme)" : "");
    }
    /* epoxy's egne hjælpere */
    void* v = dlsym(g_libepoxy, "epoxy_gl_version");
    void* ev = dlsym(g_libepoxy, "epoxy_egl_version");
    void* dg = dlsym(g_libepoxy, "epoxy_is_desktop_gl");
    void* gles = dlsym(g_libepoxy, "epoxy_is_gles");
    printf("  epoxy_gl_version=%p epoxy_egl_version=%p "
           "epoxy_is_desktop_gl=%p epoxy_is_gles=%p\n",
           v, ev, dg, gles);
}

/* Trin 2: rekonstruér libepoxy's opslagskæde (gpa -> dlsym) */
static void step_resolution_chain(void)
{
    printf("\n== 2. libepoxy-lignende opslagskæde (gpa, så dlsym) ==\n");
    g_libegl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!g_libegl) {
        printf("dlopen(libEGL.so.1) FAIL: %s\n", dlerror());
        return;
    }
    printf("dlopen(libEGL.so.1)=%p (wrapper?)\n", g_libegl);
    g_gpa = (pfn_eglGetProcAddress)dlsym(g_libegl, "eglGetProcAddress");
    printf("dlsym(libEGL, eglGetProcAddress)=%p\n", (void*)g_gpa);
    for (int i = 0; kNames[i]; i++) {
        const char* n = kNames[i];
        void* via_gpa = g_gpa ? g_gpa(n) : NULL;
        void* via_dlsym = dlsym(g_libegl, n);
        void* epoxy_export = g_libepoxy ? dlsym(g_libepoxy, n) : NULL;
        printf("  %-32s gpa=%p dlsym(h)=%p epoxy-export=%p%s%s\n",
               n, via_gpa, via_dlsym, epoxy_export,
               (via_gpa && via_gpa == via_dlsym) ? "  [gpa==dlsym]" : "",
               (epoxy_export && via_gpa && epoxy_export == via_gpa)
                   ? "  [epoxy==gpa]"
                   : "");
    }
}

/* Hjælper: kalder eglCreateContext og printer resultat + fejl */
static void try_create(const char* label, EGLDisplay dpy, EGLConfig cfg,
                       const EGLint* attrs)
{
    static pfn_eglCreateContext f = NULL;
    static pfn_eglGetError ge = NULL;
    if (!f) {
        f = (pfn_eglCreateContext)dlsym(g_libepoxy, "eglCreateContext");
        ge = (pfn_eglGetError)dlsym(g_libepoxy, "eglGetError");
    }
    if (!f || !ge) {
        printf("  [%s] mangler epoxy-eglCreateContext/eglGetError\n", label);
        return;
    }
    EGLContext ctx = f(dpy, cfg, EGL_NO_CONTEXT, attrs);
    printf("  [%s] eglCreateContext=%p err=%s\n", label, (void*)ctx,
           hexerr(ge()));
    if (ctx) {
        static pfn_eglDestroyContext dc = NULL;
        if (!dc) dc = (pfn_eglDestroyContext)dlsym(g_libepoxy,
                                                   "eglDestroyContext");
        if (dc) dc(dpy, ctx);
    }
}

/* Trin 3: hele dansen gennem libepoxy's eksporter */
static void step_dance(void)
{
    printf("\n== 3. EGL-dansen gennem libepoxy's eksporter ==\n");
    if (!g_libepoxy) {
        printf("intet libepoxy — springer over\n");
        return;
    }
    pfn_eglGetDisplay gd = (pfn_eglGetDisplay)dlsym(g_libepoxy,
                                                    "eglGetDisplay");
    pfn_eglInitialize ei = (pfn_eglInitialize)dlsym(g_libepoxy,
                                                    "eglInitialize");
    pfn_eglTerminate et = (pfn_eglTerminate)dlsym(g_libepoxy, "eglTerminate");
    pfn_eglChooseConfig ec = (pfn_eglChooseConfig)dlsym(g_libepoxy,
                                                        "eglChooseConfig");
    pfn_eglGetConfigAttrib ga = (pfn_eglGetConfigAttrib)dlsym(
        g_libepoxy, "eglGetConfigAttrib");
    pfn_eglBindAPI ba = (pfn_eglBindAPI)dlsym(g_libepoxy, "eglBindAPI");
    pfn_eglMakeCurrent mc = (pfn_eglMakeCurrent)dlsym(g_libepoxy,
                                                      "eglMakeCurrent");
    pfn_eglCreatePbufferSurface ps = (pfn_eglCreatePbufferSurface)dlsym(
        g_libepoxy, "eglCreatePbufferSurface");
    pfn_eglDestroySurface ds = (pfn_eglDestroySurface)dlsym(g_libepoxy,
                                                            "eglDestroySurface");
    pfn_eglGetError ge = (pfn_eglGetError)dlsym(g_libepoxy, "eglGetError");
    pfn_glGetString gs = (pfn_glGetString)dlsym(g_libepoxy, "glGetString");

    printf("peger: gd=%p ei=%p ec=%p ba=%p ps=%p mc=%p ge=%p gs=%p\n",
           (void*)gd, (void*)ei, (void*)ec, (void*)ba, (void*)ps, (void*)mc,
           (void*)ge, (void*)gs);

    /* Display: både EGL_DEFAULT_DISPLAY og X-Display* (Firefox brugte X*) */
    EGLDisplay dpy_default = gd ? gd(EGL_DEFAULT_DISPLAY) : NULL;
    printf("eglGetDisplay(EGL_DEFAULT_DISPLAY)=%p err=%s\n",
           (void*)dpy_default, hexerr(ge ? ge() : 0));

    void* xdpy = dlopen("libX11.so.6", RTLD_NOW);
    void* (*xopendisplay)(char*) = NULL;
    if (xdpy) xopendisplay = dlsym(xdpy, "XOpenDisplay");
    void* xd = xopendisplay ? xopendisplay(NULL) : NULL;
    printf("XOpenDisplay=%p (via dlopen(libX11))\n", xd);
    EGLDisplay dpy_x = gd ? gd((EGLNativeDisplayType)xd) : NULL;
    printf("eglGetDisplay(X-Display*)=%p err=%s\n", (void*)dpy_x,
           hexerr(ge ? ge() : 0));

    EGLDisplay dpy = dpy_default ? dpy_default : dpy_x;
    if (!dpy) {
        printf("ingen display — stopper\n");
        return;
    }
    EGLint major = 0, minor = 0;
    EGLBoolean ok = ei ? ei(dpy, &major, &minor) : EGL_FALSE;
    printf("eglInitialize=%d (%d.%d) err=%s\n", ok, major, minor,
           hexerr(ge ? ge() : 0));
    if (!ok) return;

    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR,
        EGL_NONE};
    EGLConfig configs[32];
    EGLint ncfg = 32;
    ok = ec ? ec(dpy, cfg_attrs, configs, ncfg, &ncfg) : EGL_FALSE;
    printf("eglChooseConfig(pbuffer ES2|ES3)=%d ncfg=%d err=%s\n", ok, ncfg,
           hexerr(ge ? ge() : 0));
    if (!ok || ncfg < 1) {
        printf("ingen pbuffer-config — stopper\n");
        return;
    }
    EGLConfig cfg = configs[0];
    EGLint r = 0, g = 0, b = 0, a = 0, rt = 0;
    if (ga) {
        ga(dpy, cfg, EGL_RED_SIZE, &r);
        ga(dpy, cfg, EGL_GREEN_SIZE, &g);
        ga(dpy, cfg, EGL_BLUE_SIZE, &b);
        ga(dpy, cfg, EGL_ALPHA_SIZE, &a);
        ga(dpy, cfg, EGL_RENDERABLE_TYPE, &rt);
    }
    printf("config0 RGBA=%d/%d/%d/%d renderable=0x%x\n", r, g, b, a,
           (unsigned)rt);

    EGLint pb_attrs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface pb = ps ? ps(dpy, cfg, pb_attrs) : EGL_NO_SURFACE;
    printf("eglCreatePbufferSurface=%p err=%s\n", (void*)pb,
           hexerr(ge ? ge() : 0));

    /* Mønster A: ES-vejen, MAJOR 3 (hardware-WR i Firefox) */
    printf("\n-- ES-vej (mønster A) --\n");
    ok = ba ? ba(EGL_OPENGL_ES_API) : EGL_FALSE;
    printf("eglBindAPI(ES)=%d err=%s\n", ok, hexerr(ge ? ge() : 0));
    EGLint es3[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    try_create("ES3", dpy, cfg, es3);
    EGLint es31[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                     EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    try_create("ES3.1", dpy, cfg, es31);

    /* Mønster B: desktop-GL-vejen, core 3.2 + robustness-attributter */
    printf("\n-- desktop-GL-vej (mønster B) --\n");
    ok = ba ? ba(EGL_OPENGL_API) : EGL_FALSE;
    printf("eglBindAPI(GL)=%d err=%s\n", ok, hexerr(ge ? ge() : 0));
    EGLint core32[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_NONE};
    try_create("core3.2", dpy, cfg, core32);
    EGLint khr_robust[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
        EGL_LOSE_CONTEXT_ON_RESET_KHR,
        EGL_NONE};
    try_create("core3.2+khr-robust", dpy, cfg, khr_robust);
    EGLint khr_rbab[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
        EGL_LOSE_CONTEXT_ON_RESET_KHR,
        EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR,
        EGL_NONE};
    try_create("core3.2+khr-rbab", dpy, cfg, khr_rbab);

    /* Og den egentlige makeCurrent-test: opret ES3-kontekst gennem epoxy,
     * bind pbuffer og læs GL-strenge — så Init-feberen (mønster B) kan ses. */
    printf("\n-- makeCurrent + glGetString gennem epoxy (ES3) --\n");
    ba(EGL_OPENGL_ES_API);
    EGLContext ctx = ((pfn_eglCreateContext)dlsym(g_libepoxy,
                                                  "eglCreateContext"))(
        dpy, cfg, EGL_NO_CONTEXT, es3);
    printf("es3-context via epoxy=%p err=%s\n", (void*)ctx,
           hexerr(ge ? ge() : 0));
    if (ctx) {
        ok = mc ? mc(dpy, pb, pb, ctx) : EGL_FALSE;
        printf("eglMakeCurrent(pb,pb,ctx)=%d err=%s\n", ok,
               hexerr(ge ? ge() : 0));
        if (ok && gs) {
            printf("GL_VENDOR  = %s\n", gs(0x1F00));
            printf("GL_RENDERER= %s\n", gs(0x1F01));
            printf("GL_VERSION = %s\n", gs(0x1F02));
        }
        mc(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (pb && ds) ds(dpy, pb);
    if (dpy && et) et(dpy);
    printf("\nDONE\n");
}

int main(void)
{
    printf("epoxy_mimic — libepoxy EGL-dispatch-test (boks 1)\n");
    printf("LD_LIBRARY_PATH=%s EGL_PLATFORM=%s DISPLAY=%s\n",
           getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(null)",
           getenv("EGL_PLATFORM") ? getenv("EGL_PLATFORM") : "(null)",
           getenv("DISPLAY") ? getenv("DISPLAY") : "(null)");
    print_maps_egl();
    step_epoxy_exports();
    step_resolution_chain();
    step_dance();
    print_maps_egl();
    return 0;
}
