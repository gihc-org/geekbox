/*
 * desktop_create_probe.c — hvilke config/API/attrib-kombinationer virker?
 * Efter bindapi-patch (eglBindAPI = no-op TRUE, currentApi=0):
 *   ES-attempt: config 0x2 (GL-only) + ES3 → 0x3009 BAD_MATCH
 *   GL-attempt: config 0x12 (ES-only) + GL core 3.2 → ? (gdb2: create OK, Init fejler)
 * Proben tester systematisk: config 0x2/0x12 × ES3/GL-core × attrib-sæt.
 */
#include <dlfcn.h>
#include <stdio.h>
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
#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR 0x31bd
#endif
#ifndef EGL_LOSE_CONTEXT_ON_RESET_KHR
#define EGL_LOSE_CONTEXT_ON_RESET_KHR 0x31bf
#endif
#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT 0x3138
#endif
#ifndef EGL_LOSE_CONTEXT_ON_RESET_EXT
#define EGL_LOSE_CONTEXT_ON_RESET_EXT 0x31bf
#endif
#ifndef EGL_CONTEXT_FLAGS_KHR
#define EGL_CONTEXT_FLAGS_KHR 0x30fc
#endif
#ifndef EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR 0x00000004
#endif
#ifndef EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT 0x30bf
#endif

typedef EGLDisplay (*p_gd)(EGLNativeDisplayType);
typedef EGLBoolean (*p_init)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*p_cc)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*p_ba)(EGLenum);
typedef EGLContext (*p_cr)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLint (*p_err)(void);

static p_gd gd;
static p_init init;
static p_cc cc;
static p_ba ba;
static p_cr cr;
static p_err err;

static const char* hexerr(void) {
    static char b[16];
    snprintf(b, sizeof(b), "0x%x", (unsigned)err());
    return b;
}

static EGLConfig pick(const EGLint* attrs) {
    EGLConfig cfg[8];
    EGLint n = 8;
    EGLDisplay d = gd(EGL_DEFAULT_DISPLAY);
    EGLint maj = 0, min = 0;
    init(d, &maj, &min);
    if (!cc(d, attrs, cfg, n, &n) || n < 1) return NULL;
    return cfg[0];
}

static void try_create(const char* label, EGLConfig cfg, EGLenum api,
                       const EGLint* attrs) {
    EGLDisplay d = gd(EGL_DEFAULT_DISPLAY);
    EGLBoolean bok = ba(api);
    EGLContext c = cr(d, cfg, EGL_NO_CONTEXT, attrs);
    printf("%-34s api=0x%x cfg=%p bind=%d ctx=%p err=%s\n",
           label, api, (void*)cfg, bok, (void*)c, hexerr());
}

int main(void) {
    void* h = dlopen("libEGL.so.1", RTLD_LAZY);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    gd = (p_gd)dlsym(h, "eglGetDisplay");
    init = (p_init)dlsym(h, "eglInitialize");
    cc = (p_cc)dlsym(h, "eglChooseConfig");
    ba = (p_ba)dlsym(h, "eglBindAPI");
    cr = (p_cr)dlsym(h, "eglCreateContext");
    err = (p_err)dlsym(h, "eglGetError");

    EGLint gl_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                         EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                         EGL_RENDERABLE_TYPE, 0, EGL_NONE, 0};
    EGLint es_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                         EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                         EGL_NONE, 0};
    EGLConfig cfg_gl = pick(gl_attrs);
    EGLConfig cfg_es = pick(es_attrs);
    printf("cfg_gl=%p cfg_es=%p\n", (void*)cfg_gl, (void*)cfg_es);

    EGLint es3[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLint core32[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK,
                       EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                       EGL_CONTEXT_MAJOR_VERSION, 3,
                       EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    EGLint khr_rbab[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                         EGL_CONTEXT_MAJOR_VERSION, 3,
                         EGL_CONTEXT_MINOR_VERSION, 2,
                         EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                         EGL_LOSE_CONTEXT_ON_RESET_KHR,
                         EGL_CONTEXT_FLAGS_KHR,
                         EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR, EGL_NONE};
    EGLint ext_rbab[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                         EGL_CONTEXT_MAJOR_VERSION, 3,
                         EGL_CONTEXT_MINOR_VERSION, 2,
                         EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
                         EGL_LOSE_CONTEXT_ON_RESET_EXT,
                         EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT, 1, EGL_NONE};

    /* STANDARD-EGL-værdier (den lokale egl.h er ikke-standard!):
     * EGL_OPENGL_API=0x30A0, EGL_OPENGL_ES_API=0x30A1, ES2=0x30A2 */
    enum { EGL_GL = 0x30A0, EGL_ES = 0x30A1, EGL_ES2 = 0x30A2 };

    printf("\n-- ES3 (MAJOR 3) --\n");
    try_create("ES3 paa GL-config", cfg_gl, EGL_ES, es3);
    try_create("ES3 paa ES-config", cfg_es, EGL_ES, es3);
    try_create("ES3 paa GL-config (ES2-bind)", cfg_gl, EGL_ES2, es3);
    try_create("ES3 paa ES-config (ES2-bind)", cfg_es, EGL_ES2, es3);

    printf("\n-- GL core 3.2 --\n");
    try_create("GL-core paa GL-config", cfg_gl, EGL_GL, core32);
    try_create("GL-core paa ES-config", cfg_es, EGL_GL, core32);

    printf("\n-- GL core 3.2 + khr_rbab --\n");
    try_create("khr_rbab paa GL-config", cfg_gl, EGL_GL, khr_rbab);
    try_create("khr_rbab paa ES-config", cfg_es, EGL_GL, khr_rbab);

    printf("\n-- GL core 3.2 + ext_rbab --\n");
    try_create("ext_rbab paa GL-config", cfg_gl, EGL_GL, ext_rbab);
    try_create("ext_rbab paa ES-config", cfg_es, EGL_GL, ext_rbab);

    printf("\nDONE\n");
    return 0;
}
