/* min_es_create.c — minimal test: én create på config 0x12 med Firefox'
 * ES-attribs (khr_rbab / ext_rbab / required) — intet andet før. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>

typedef EGLDisplay (*p_gd)(EGLNativeDisplayType);
typedef EGLBoolean (*p_init)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*p_cc)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*p_ba)(EGLenum);
typedef EGLContext (*p_cr)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLint (*p_err)(void);
typedef EGLBoolean (*p_gca)(EGLDisplay, EGLConfig, EGLint, EGLint*);
typedef EGLSurface (*p_cps)(EGLDisplay, EGLConfig, const EGLint*);

int main(void) {
    const char* lib = getenv("EGLLIB") ? getenv("EGLLIB") : "libEGL.so.1";
    void* h = dlopen(lib, RTLD_LAZY);
    if (getenv("BOTH")) {
        void* h2 = dlopen("libEGL.so.1", RTLD_LAZY);
        printf("begge instanser: h=%p h2=%p\n", h, h2);
        /* brug .so-instansens create hvis h er .so.1 og omvendt? Nej: behold h */
    }
    printf("dlopen(%s)=%p\n", lib, h);
    p_gd gd = (p_gd)dlsym(h, "eglGetDisplay");
    p_init init = (p_init)dlsym(h, "eglInitialize");
    p_cc cc = (p_cc)dlsym(h, "eglChooseConfig");
    p_ba ba = (p_ba)dlsym(h, "eglBindAPI");
    p_cr cr = (p_cr)dlsym(h, "eglCreateContext");
    p_err err = (p_err)dlsym(h, "eglGetError");

    void* xh = dlopen("libX11.so.6", RTLD_NOW);
    void* (*xopen)(char*) = xh ? dlsym(xh, "XOpenDisplay") : NULL;
    void* xd = xopen ? xopen(NULL) : NULL;
    EGLDisplay d = (xd && getenv("XDISPLAY"))
                       ? gd((EGLNativeDisplayType)xd)
                       : gd(EGL_DEFAULT_DISPLAY);
    printf("xdpy=%p d=%p\n", xd, (void*)d);
    EGLint maj = 0, min = 0;
    init(d, &maj, &min);
    EGLint es_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                         EGL_ALPHA_SIZE, 8,
                         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE, 0};
    EGLConfig cfgs[16];
    EGLint n = 16;
    cc(d, es_attrs, cfgs, 16, &n);
    int pick = 0;
    const char* which = getenv("CFG");
    if (which && *which) pick = atoi(which);
    EGLConfig cfg = cfgs[pick];
    printf("cfg[%d]=%p n=%d\n", pick, (void*)cfg, n);

    const char* only = getenv("ONLY");

    /* Firefox desktop-attempt-sæt (fra gdb): MAJOR 3 + khr/ext robustness */
    EGLint khr_rbab[] = {0x3098, 3, 0x31bd, 0x31bf, 0x30fc, 4, 0x3038, 0};
    EGLint ext_rbab[] = {0x3098, 3, 0x3138, 0x31bf, 0x30bf, 1, 0x3038, 0};
    EGLint required[] = {0x3098, 3, 0x3038, 0};

    const char* api = getenv("API");
    EGLenum apiv = (api && !strcmp(api, "es")) ? 0x30A2 : 0x30A0;
    if (getenv("SEQ")) {
        if (getenv("PRELOAD_EXT")) {
            typedef void* (*p_gpa)(const char*);
            p_gpa gpa = (p_gpa)dlsym(h, "eglGetProcAddress");
            const char* names[] = {
                "eglGetNativeClientBufferANDROID", "eglQuerySurfacePointerANGLE",
                "eglCreateSyncKHR", "eglDestroySyncKHR", "eglClientWaitSyncKHR",
                "eglGetSyncAttribKHR", "eglCreateImageKHR", "eglDestroyImageKHR",
                "eglWaitSyncKHR", "eglDupNativeFenceFDANDROID",
                "eglCreateStreamKHR", "eglDestroyStreamKHR", "eglQueryStreamKHR",
                "eglStreamConsumerGLTextureExternalKHR",
                "eglStreamConsumerAcquireKHR", "eglStreamConsumerReleaseKHR",
                "eglQueryDisplayAttribEXT", "eglQueryDeviceAttribEXT",
                "eglQueryDeviceStringEXT", "eglGetPlatformDisplay",
                "eglQueryDevicesEXT", NULL};
            for (int i = 0; names[i]; i++) {
                void* p = gpa(names[i]);
                printf("gpa(%s)=%p\n", names[i], p);
            }
        }
        if (getenv("FINDVISUAL")) {
            EGLint fv_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                 EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_NONE, 0};
            EGLConfig fc[16];
            EGLint fn = 16;
            cc(d, fv_attrs, fc, 16, &fn);
            printf("FindVisual-choose: n=%d first=%p\n", fn, (void*)fc[0]);
            EGLint vid = 0;
            if (fn > 0) {
                p_gca gca = (p_gca)dlsym(h, "eglGetConfigAttrib");
                gca(d, fc[0], 0x302e, &vid);
                printf("FindVisual vid=0x%x\n", vid);
            }
        }
        if (getenv("PBUFFER")) {
            p_cps cps = (p_cps)dlsym(h, "eglCreatePbufferSurface");
            EGLint pb[] = {0x3057, 16, 0x3056, 16, 0x3038, 0};
            EGLSurface s = cps(d, cfg, pb);
            printf("pbuffer=%p err=0x%x\n", (void*)s, (unsigned)err());
        }
        if (getenv("DUAL")) {
            /* Hele Firefox-flowet: C(RT=0)-valg → ES-forsøg → C(RT=ES2)-valg
             * → desktop-forsøg, hver med RGBA8888-filter som CreateConfig. */
            EGLint rt0[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                            EGL_RENDERABLE_TYPE, 0, EGL_NONE, 0};
            EGLint es2[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE, 0};
            p_gca gca = (p_gca)dlsym(h, "eglGetConfigAttrib");
            EGLConfig c1 = 0, c2 = 0;
            EGLConfig lst[16]; EGLint n = 16;
            cc(d, rt0, lst, 16, &n);
            for (int i = 0; i < n; i++) {
                EGLint r = 0, g = 0, b = 0, a = 0;
                gca(d, lst[i], 0x3024, &r); gca(d, lst[i], 0x3023, &g);
                gca(d, lst[i], 0x3022, &b); gca(d, lst[i], 0x3021, &a);
                if (r == 8 && g == 8 && b == 8 && a == 8) { c1 = lst[i]; break; }
            }
            n = 16;
            cc(d, es2, lst, 16, &n);
            for (int i = 0; i < n; i++) {
                EGLint r = 0, g = 0, b = 0, a = 0;
                gca(d, lst[i], 0x3024, &r); gca(d, lst[i], 0x3023, &g);
                gca(d, lst[i], 0x3022, &b); gca(d, lst[i], 0x3021, &a);
                if (r == 8 && g == 8 && b == 8 && a == 8) { c2 = lst[i]; break; }
            }
            printf("DUAL: c1=%p c2=%p\n", (void*)c1, (void*)c2);
            EGLint pm_ext[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x3138, 0x31bf,
                               0x30bf, 1, 0x3038, 0};
            EGLint pm_req[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x3038, 0};
            ba(0x30A2);
            EGLContext e1 = cr(d, c1, EGL_NO_CONTEXT, pm_ext);
            printf("ES pm_ext (c1): %p e=%x\n", (void*)e1, (unsigned)err());
            EGLContext e2 = cr(d, c1, EGL_NO_CONTEXT, pm_req);
            printf("ES pm_req (c1): %p e=%x\n", (void*)e2, (unsigned)err());
            ba(0x30A0);
            EGLContext g1 = cr(d, c2, EGL_NO_CONTEXT, ext_rbab);
            printf("GL ext (c2):    %p e=%x\n", (void*)g1, (unsigned)err());
            EGLContext g2 = cr(d, c2, EGL_NO_CONTEXT, required);
            printf("GL req (c2):    %p e=%x\n", (void*)g2, (unsigned)err());
            fflush(stdout);
            return 0;
        }
        if (getenv("ATTRPROBE")) {
            EGLint v_req[] = {0x3098, 3, 0x30fb, 2, 0x3038, 0};
            EGLint v_ext[] = {0x3098, 3, 0x30fb, 2, 0x3138, 0x31bf, 0x30bf, 1,
                              0x3038, 0};
            EGLint v_khr[] = {0x3098, 3, 0x30fb, 2, 0x31bd, 0x31bf, 0x30fc, 4,
                              0x3038, 0};
            EGLint no_minor_req[] = {0x3098, 3, 0x3038, 0};
            ba(0x30A2);
            EGLContext a = cr(d, cfg, EGL_NO_CONTEXT, v_req);
            printf("minor2 req:     %p e=%x\n", (void*)a, (unsigned)err());
            EGLContext b = cr(d, cfg, EGL_NO_CONTEXT, v_ext);
            printf("minor2 ext:     %p e=%x\n", (void*)b, (unsigned)err());
            EGLContext c = cr(d, cfg, EGL_NO_CONTEXT, v_khr);
            printf("minor2 khr:     %p e=%x\n", (void*)c, (unsigned)err());
            EGLContext d2 = cr(d, cfg, EGL_NO_CONTEXT, no_minor_req);
            printf("no-minor req:   %p e=%x\n", (void*)d2, (unsigned)err());
            fflush(stdout);
            return 0;
        }
        /* Hele Firefox-sekvensen i én proces: ES-forsøg (med profile-mask)
         * efterfulgt af desktop-forsøg (uden profile-mask) — på samme config. */
        EGLint pm_khr[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x31bd, 0x31bf,
                           0x30fc, 4, 0x3038, 0};
        EGLint pm_ext[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x3138, 0x31bf,
                           0x30bf, 1, 0x3038, 0};
        EGLint pm_khr2[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x31bd, 0x31bf,
                            0x3038, 0};
        EGLint pm_ext2[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x3138, 0x31bf,
                            0x3038, 0};
        EGLint pm_req[] = {0x30fd, 1, 0x3098, 3, 0x30fb, 2, 0x3038, 0};
        ba(0x30A2);
        EGLContext c;
        c = cr(d, cfg, EGL_NO_CONTEXT, pm_khr);  printf("ES pm_khr:  %p e=%x\n", (void*)c, (unsigned)err());
        c = cr(d, cfg, EGL_NO_CONTEXT, pm_ext);  printf("ES pm_ext:  %p e=%x\n", (void*)c, (unsigned)err());
        c = cr(d, cfg, EGL_NO_CONTEXT, pm_khr2); printf("ES pm_khr2: %p e=%x\n", (void*)c, (unsigned)err());
        c = cr(d, cfg, EGL_NO_CONTEXT, pm_ext2); printf("ES pm_ext2: %p e=%x\n", (void*)c, (unsigned)err());
        c = cr(d, cfg, EGL_NO_CONTEXT, pm_req);  printf("ES pm_req:  %p e=%x\n", (void*)c, (unsigned)err());
        if (getenv("REINIT")) {
            EGLDisplay d2 = gd(EGL_DEFAULT_DISPLAY);
            EGLint m2 = 0, n2 = 0;
            init(d2, &m2, &n2);
            printf("REINIT: d2=%p init=%d (%d.%d)\n", (void*)d2, 1, m2, n2);
        }
        ba(0x30A0);
        c = cr(d, cfg, EGL_NO_CONTEXT, khr_rbab); printf("GL khr:    %p e=%x\n", (void*)c, (unsigned)err());
        c = cr(d, cfg, EGL_NO_CONTEXT, ext_rbab); printf("GL ext:    %p e=%x\n", (void*)c, (unsigned)err());
        c = cr(d, cfg, EGL_NO_CONTEXT, required); printf("GL req:    %p e=%x\n", (void*)c, (unsigned)err());
        fflush(stdout);
        return 0;
    }
    ba(apiv);
    if (!only || !strcmp(only, "khr")) {
        EGLContext c = cr(d, cfg, EGL_NO_CONTEXT, khr_rbab);
        printf("khr_rbab: ctx=%p err=0x%x\n", (void*)c, (unsigned)err());
    }
    if (!only || !strcmp(only, "ext")) {
        EGLContext c = cr(d, cfg, EGL_NO_CONTEXT, ext_rbab);
        printf("ext_rbab: ctx=%p err=0x%x\n", (void*)c, (unsigned)err());
    }
    if (!only || !strcmp(only, "req")) {
        EGLContext c = cr(d, cfg, EGL_NO_CONTEXT, required);
        printf("required: ctx=%p err=0x%x\n", (void*)c, (unsigned)err());
    }
    fflush(stdout);
    return 0;
}
