/* firefox_seq_probe.c — replikerer firefox-esr's eksakte EGL-sekvens fra
 * gdb-sporet (24. aug 2026, efter bindapi+chooseConfig+driver-patches):
 *
 *   1. CreateConfig(RGBA32, no RT): chooseConfig {WINDOW, R8G8B8A8, NONE}
 *   2. CreateConfig(RGBA32, ES2):   chooseConfig {WINDOW, R8G8B8A8, RT=ES2, NONE}
 *   3. for hver config: bindAPI(ES2/GL) + create med Firefox' attrib-lister:
 *      ES:  pm_khr {0x30fd:1, 0x3098:3, 0x30fb:2, 0x31bd:0x31bf, 0x30fc:4}
 *           pm_ext {0x30fd:1, 0x3098:3, 0x30fb:2, 0x3138:0x31bf, 0x30bf:1}
 *      GL:  khr    {0x3098:3, 0x31bd:0x31bf, 0x30fc:4}
 *           ext    {0x3098:3, 0x3138:0x31bf, 0x30bf:1}
 *           req    {0x3098:3}
 *
 * Mål: find hvilket led der giver NULL/0x3000 isoleret (uden Firefox).
 * Byg på boksen:
 *   gcc -O0 -g -o /root/firefox_seq_probe firefox_seq_probe.c \
 *       -I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm
 * Kør:
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *   EGL_PLATFORM=x11 DISPLAY=:0 /root/firefox_seq_probe
 */
#include <EGL/egl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef EGLDisplay (*p_gd)(EGLNativeDisplayType);
typedef EGLBoolean (*p_init)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*p_cc)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*p_gca)(EGLDisplay, EGLConfig, EGLint, EGLint*);
typedef EGLBoolean (*p_ba)(EGLenum);
typedef EGLContext (*p_cr)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLint (*p_err)(void);

static void dump_cfg(p_gca gca, EGLDisplay d, EGLConfig c) {
    EGLint id=0, r=0, g=0, b=0, a=0, rt=0, surf=0, vid=0, depth=0;
    gca(d, c, 0x3028, &id);   /* CONFIG_ID */
    gca(d, c, 0x3024, &r);    /* RED_SIZE */
    gca(d, c, 0x3023, &g);    /* GREEN_SIZE */
    gca(d, c, 0x3022, &b);    /* BLUE_SIZE */
    gca(d, c, 0x3021, &a);    /* ALPHA_SIZE */
    gca(d, c, 0x3040, &rt);   /* RENDERABLE_TYPE */
    gca(d, c, 0x3033, &surf); /* SURFACE_TYPE */
    gca(d, c, 0x302e, &vid);  /* NATIVE_VISUAL_ID */
    gca(d, c, 0x3025, &depth);/* DEPTH_SIZE */
    printf("  cfg=%p id=%d rgba=%d/%d/%d/%d rt=0x%x surf=0x%x vid=%d depth=%d\n",
           (void*)c, id, r, g, b, a, rt, surf, vid, depth);
}

static void choose_all(p_cc cc, p_gca gca, EGLDisplay d, const EGLint* attrs,
                       const char* tag) {
    EGLConfig cfgs[64];
    EGLint n = 64;
    printf("== chooseConfig %s ==\n", tag);
    if (!cc(d, attrs, cfgs, 64, &n) || n < 1) {
        printf("  FAILED n=%d\n", n);
        return;
    }
    printf("  n=%d\n", n);
    for (int i = 0; i < n && i < 64; i++) dump_cfg(gca, d, cfgs[i]);
}

static void try_create(const char* label, p_ba ba, p_cr cr, p_err err,
                       EGLDisplay d, EGLConfig cfg, EGLenum api,
                       const EGLint* attrs) {
    ba(api);
    EGLContext c = cr(d, cfg, EGL_NO_CONTEXT, attrs);
    printf("%-28s api=0x%x ctx=%p err=0x%x\n", label, (unsigned)api,
           (void*)c, (unsigned)err());
    fflush(stdout);
}

/* Firefox' GLLibraryEGL::Init-preamble (målt via gdb 24. aug 2026):
 * ~28 eglGetProcAddress-kald før eglChooseConfig/eglCreateContext. */
static void firefox_preamble(void) {
    typedef void* (*p_gpa)(const char*);
    void* h = dlopen("libEGL.so.1", RTLD_LAZY);
    p_gpa gpa = h ? (p_gpa)dlsym(h, "eglGetProcAddress") : NULL;
    const char* names[] = {
        "eglGetNativeClientBufferANDROID", "eglQuerySurfacePointerANGLE",
        "eglCreateSyncKHR", "eglDestroySyncKHR", "eglClientWaitSyncKHR",
        "eglGetSyncAttribKHR", "eglCreateImageKHR", "eglDestroyImageKHR",
        "eglWaitSyncKHR", "eglDupNativeFenceFDANDROID",
        "eglCreateStreamKHR", "eglDestroyStreamKHR", "eglQueryStreamKHR",
        "eglStreamConsumerGLTextureExternalKHR",
        "eglStreamConsumerAcquireKHR", "eglStreamConsumerReleaseKHR",
        "eglQueryDisplayAttribEXT", "eglQueryDeviceAttribEXT",
        "eglQueryDeviceStringEXT", "eglStreamConsumerGLTextureExternalAttribsNV",
        "eglCreateStreamProducerD3DTextureANGLE",
        "eglStreamPostD3DTextureANGLE", "eglSwapBuffersWithDamageEXT",
        "eglSwapBuffersWithDamageKHR", "eglSetDamageRegionKHR",
        "eglGetPlatformDisplay", "eglExportDMABUFImageQueryMESA",
        "eglExportDMABUFImageMESA", "eglQueryDevicesEXT", NULL};
    if (!gpa) {
        printf("## preamble: ingen eglGetProcAddress!\n");
        return;
    }
    for (int i = 0; names[i]; i++) {
        void* p = gpa(names[i]);
        printf("preamble gpa(%s)=%p\n", names[i], p);
    }
    fflush(stdout);
}

int main(void) {
    if (getenv("PREAMBLE")) firefox_preamble();

    void* h = dlopen("libEGL.so.1", RTLD_LAZY);
    printf("dlopen=%p\n", h);
    p_gd gd = (p_gd)dlsym(h, "eglGetDisplay");
    p_init init = (p_init)dlsym(h, "eglInitialize");
    p_cc cc = (p_cc)dlsym(h, "eglChooseConfig");
    p_gca gca = (p_gca)dlsym(h, "eglGetConfigAttrib");
    p_ba ba = (p_ba)dlsym(h, "eglBindAPI");
    p_cr cr = (p_cr)dlsym(h, "eglCreateContext");
    p_err err = (p_err)dlsym(h, "eglGetError");

    void* xh = dlopen("libX11.so.6", RTLD_NOW);
    void* (*xopen)(char*) = xh ? dlsym(xh, "XOpenDisplay") : NULL;
    void* xd = xopen ? xopen(NULL) : NULL;
    /* Firefox videregiver sin X-Display til eglGetDisplay. Brug det som
     * standard; XDISPLAY=0 tvinger EGL_DEFAULT_DISPLAY for sammenligning. */
    const char* xdisp = getenv("XDISPLAY");
    EGLDisplay d = (xd && (!xdisp || strcmp(xdisp, "0")))
                       ? gd((EGLNativeDisplayType)xd)
                       : gd(EGL_DEFAULT_DISPLAY);
    EGLint maj = 0, min = 0;
    printf("xdpy=%p d=%p\n", xd, (void*)d);
    if (!init(d, &maj, &min)) {
        printf("eglInitialize FAILED err=0x%x\n", (unsigned)err());
        return 1;
    }
    printf("init ok %d.%d\n", maj, min);

    /* Firefox' attrib-sæt fra gdb (CreateConfig RGBA32) */
    EGLint rgba32_nort[] = {0x3033, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8,
                            0x3021, 8, 0x3038, 0};
    EGLint rgba32_es2[] = {0x3033, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8,
                           0x3021, 8, 0x3040, 4, 0x3038, 0};

    choose_all(cc, gca, d, rgba32_nort, "RGBA32 no-RT");
    choose_all(cc, gca, d, rgba32_es2, "RGBA32 ES2");

    /* Firefox' create-attribs (præcis som gdb fangede dem) */
    EGLint pm_khr[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x31bd, 0x31bf,
                       0x30fc, 4, 0x3038, 0};
    EGLint pm_ext[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x3138, 0x31bf,
                       0x30bf, 1, 0x3038, 0};
    EGLint khr[] = {0x3098, 3, 0x31bd, 0x31bf, 0x30fc, 4, 0x3038, 0};
    EGLint ext[] = {0x3098, 3, 0x3138, 0x31bf, 0x30bf, 1, 0x3038, 0};
    EGLint req[] = {0x3098, 3, 0x3038, 0};

    /* Prøv på hver no-RT-config (Firefox ES-forsøg) og ES2-config
     * (Firefox GL-forsøg), så vi ser om config-valget er årsagen. */
    EGLConfig cfgs[64];
    EGLint n = 64;
    cc(d, rgba32_nort, cfgs, 64, &n);
    for (int i = 0; i < n && i < 64; i++) {
        printf("--- config %d ---\n", i);
        try_create("ES pm_khr", ba, cr, err, d, cfgs[i], 0x30A2, pm_khr);
        try_create("ES pm_ext", ba, cr, err, d, cfgs[i], 0x30A2, pm_ext);
    }
    n = 64;
    cc(d, rgba32_es2, cfgs, 64, &n);
    for (int i = 0; i < n && i < 64; i++) {
        printf("--- ES2-config %d ---\n", i);
        try_create("GL khr", ba, cr, err, d, cfgs[i], 0x30A0, khr);
        try_create("GL ext", ba, cr, err, d, cfgs[i], 0x30A0, ext);
        try_create("GL req", ba, cr, err, d, cfgs[i], 0x30A0, req);
    }
    return 0;
}
