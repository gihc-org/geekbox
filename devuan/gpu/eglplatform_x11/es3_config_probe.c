/*
 * es3_config_probe.c — hvorfor fejler Firefox' ES-vej med EGL_BAD_MATCH?
 *
 * Firefox (CreateWithoutSurface → ChooseConfig → FillContextAttribs) beder om:
 *   EGL_SURFACE_TYPE=PBUFFER_BIT, RENDERABLE_TYPE=ES3_BIT, RGB8888,
 *   DEPTH 0, STENCIL 0 (+ dobbelt-EGL_NONE-terminering)
 * og opretter derefter en ES3-kontekst (MAJOR 3 + robustness-attributter) på
 * den valgte config. Android-loaderens eglCreateContext afviser med 0x3009
 * (EGL_BAD_MATCH), hvis config'ens renderable-type ikke har ES3-bit'en.
 *
 * Proben viser hvilke configs wrapperen/Android returnerer for hhv. ES3- og
 * ES2-forespørgsel, deres renderable-types, og om eglCreateContext virker.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30fb
#endif

typedef EGLDisplay (*p_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*p_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*p_eglChooseConfig)(EGLDisplay, const EGLint*,
                                        EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*p_eglGetConfigAttrib)(EGLDisplay, EGLConfig,
                                           EGLint, EGLint*);
typedef EGLBoolean (*p_eglBindAPI)(EGLenum);
typedef EGLContext (*p_eglCreateContext)(EGLDisplay, EGLConfig,
                                         EGLContext, const EGLint*);
typedef EGLint (*p_eglGetError)(void);

static p_eglGetDisplay f_eglGetDisplay;
static p_eglInitialize f_eglInitialize;
static p_eglChooseConfig f_eglChooseConfig;
static p_eglGetConfigAttrib f_eglGetConfigAttrib;
static p_eglBindAPI f_eglBindAPI;
static p_eglCreateContext f_eglCreateContext;
static p_eglGetError f_eglGetError;

static const char* hexerr(void) {
    static char b[16];
    snprintf(b, sizeof(b), "0x%x", (unsigned)f_eglGetError());
    return b;
}

static void probe_configs(EGLDisplay dpy, const char* label,
                          const EGLint* attribs) {
    EGLConfig configs[64];
    EGLint ncfg = 64;
    EGLBoolean ok = f_eglChooseConfig(dpy, attribs, configs, ncfg, &ncfg);
    printf("\n== chooseConfig[%s]: ok=%d ncfg=%d err=%s\n",
           label, ok, ncfg, hexerr());
    for (int i = 0; i < ncfg && i < 12; i++) {
        EGLint rt = 0, r = 0, g = 0, b = 0, a = 0, vid = 0, depth = 0;
        f_eglGetConfigAttrib(dpy, configs[i], EGL_RENDERABLE_TYPE, &rt);
        f_eglGetConfigAttrib(dpy, configs[i], EGL_RED_SIZE, &r);
        f_eglGetConfigAttrib(dpy, configs[i], EGL_GREEN_SIZE, &g);
        f_eglGetConfigAttrib(dpy, configs[i], EGL_BLUE_SIZE, &b);
        f_eglGetConfigAttrib(dpy, configs[i], EGL_ALPHA_SIZE, &a);
        f_eglGetConfigAttrib(dpy, configs[i], EGL_NATIVE_VISUAL_ID, &vid);
        f_eglGetConfigAttrib(dpy, configs[i], EGL_DEPTH_SIZE, &depth);
        printf("  config=%p idx=%d RGBA=%d/%d/%d/%d depth=%d vid=0x%x "
               "renderable=0x%x%s%s\n",
               (void*)configs[i], i, r, g, b, a, depth, vid, (unsigned)rt,
               (rt & EGL_OPENGL_ES3_BIT_KHR) ? " [ES3]" : "",
               (rt & EGL_OPENGL_ES2_BIT) ? " [ES2]" : "");
        /* prøv ES3-kontekst på hver config */
        f_eglBindAPI(EGL_OPENGL_ES_API);
        EGLint es3[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
        EGLContext ctx = f_eglCreateContext(dpy, configs[i], EGL_NO_CONTEXT, es3);
        printf("     -> create(ES3 MAJOR3): %p err=%s\n",
               (void*)ctx, hexerr());
    }
}

int main(void) {
    void* h = dlopen("libEGL.so.1", RTLD_LAZY);
    if (!h) { printf("dlopen libEGL.so.1 FAIL: %s\n", dlerror()); return 1; }
    f_eglGetDisplay = (p_eglGetDisplay)dlsym(h, "eglGetDisplay");
    f_eglInitialize = (p_eglInitialize)dlsym(h, "eglInitialize");
    f_eglChooseConfig = (p_eglChooseConfig)dlsym(h, "eglChooseConfig");
    f_eglGetConfigAttrib = (p_eglGetConfigAttrib)dlsym(h, "eglGetConfigAttrib");
    f_eglBindAPI = (p_eglBindAPI)dlsym(h, "eglBindAPI");
    f_eglCreateContext = (p_eglCreateContext)dlsym(h, "eglCreateContext");
    f_eglGetError = (p_eglGetError)dlsym(h, "eglGetError");
    printf("es3_config_probe — wrapper-symboler: gd=%p init=%p cc=%p "
           "gca=%p bind=%p create=%p err=%p\n",
           (void*)f_eglGetDisplay, (void*)f_eglInitialize, (void*)f_eglChooseConfig,
           (void*)f_eglGetConfigAttrib, (void*)f_eglBindAPI,
           (void*)f_eglCreateContext, (void*)f_eglGetError);

    EGLDisplay dpy = f_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    EGLBoolean ok = f_eglInitialize(dpy, &major, &minor);
    printf("eglGetDisplay=%p eglInitialize=%d (%d.%d) err=%s\n",
           (void*)dpy, ok, major, minor, hexerr());
    if (!ok) return 2;

    /* Firefox ES-vej (hardware-WR): ES3_BIT + RGB8888 + pbuffer */
    EGLint es3_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_NONE, 0, 0, 0};
    probe_configs(dpy, "Firefox-ES3 (PBUFFER|ES3|RGB8888)", es3_attrs);

    /* Firefox CreateConfig-vej (ES2_BIT + RGB565, ingen depth) */
    EGLint es2_565[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
        EGL_NONE, 0};
    probe_configs(dpy, "CreateConfig-ES2 (RGB565)", es2_565);

    /* Desktop-GL-vej (pattern B): ingen renderable-type, RGB8888 */
    EGLint gl_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE, 0};
    probe_configs(dpy, "Desktop-GL (RGB8888, ingen RT)", gl_attrs);

    /* GLContextEGLFactory::CreateImpl (compositor-widget, Linux, bpp=32):
     *   kEGLConfigAttribsRGBA32 = {WINDOW_BIT, R8 G8 B8 A8}
     *   + ES2_BIT hvis useGles */
    EGLint impl_es2[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE, 0};
    probe_configs(dpy, "CreateImpl-ES2 (WINDOW|RGB8888A8|ES2)", impl_es2);

    EGLint impl_es3[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_NONE, 0};
    probe_configs(dpy, "CreateImpl-ES3 (WINDOW|RGB8888A8|ES3)", impl_es3);

    EGLint impl_gl[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE, 0};
    probe_configs(dpy, "CreateImpl-GL (WINDOW|RGB8888A8, ingen RT)", impl_gl);

    printf("\nDONE\n");
    return 0;
}
