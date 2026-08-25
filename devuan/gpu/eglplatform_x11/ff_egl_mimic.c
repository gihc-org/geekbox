#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef GL_VENDOR
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#endif

typedef EGLDisplay (*p_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*p_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*p_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*p_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint*);
typedef EGLSurface (*p_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
typedef EGLBoolean (*p_eglBindAPI)(EGLenum);
typedef EGLContext (*p_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean (*p_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLint (*p_eglGetError)(void);
typedef const GLubyte* (*p_glGetString)(GLenum);

int main(void) {
    void* h = dlopen("libEGL.so", RTLD_LAZY);
    if (!h) { printf("dlopen libEGL.so FAIL: %s\n", dlerror()); return 1; }
    printf("dlopen(libEGL.so)=%p\n", h);
    p_eglGetDisplay eglGetDisplay = (p_eglGetDisplay)dlsym(h, "eglGetDisplay");
    p_eglInitialize eglInitialize = (p_eglInitialize)dlsym(h, "eglInitialize");
    p_eglChooseConfig eglChooseConfig = (p_eglChooseConfig)dlsym(h, "eglChooseConfig");
    p_eglGetConfigAttrib eglGetConfigAttrib = (p_eglGetConfigAttrib)dlsym(h, "eglGetConfigAttrib");
    p_eglCreateWindowSurface eglCreateWindowSurface = (p_eglCreateWindowSurface)dlsym(h, "eglCreateWindowSurface");
    p_eglBindAPI eglBindAPI = (p_eglBindAPI)dlsym(h, "eglBindAPI");
    p_eglCreateContext eglCreateContext = (p_eglCreateContext)dlsym(h, "eglCreateContext");
    p_eglMakeCurrent eglMakeCurrent = (p_eglMakeCurrent)dlsym(h, "eglMakeCurrent");
    p_eglGetError eglGetError = (p_eglGetError)dlsym(h, "eglGetError");
    p_glGetString glGetString = (p_glGetString)dlsym(RTLD_DEFAULT, "glGetString");

    printf("dlsym: eglGetDisplay=%p eglInitialize=%p eglChooseConfig=%p eglCreateWindowSurface=%p eglCreateContext=%p eglGetError=%p glGetString=%p\n",
           (void*)eglGetDisplay, (void*)eglInitialize, (void*)eglChooseConfig,
           (void*)eglCreateWindowSurface, (void*)eglCreateContext, (void*)eglGetError, (void*)glGetString);

    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) { printf("XOpenDisplay FAIL\n"); return 1; }
    int scr = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 50, 50, 960, 540, 0, 0, 0);
    XMapWindow(dpy, win);
    XSync(dpy, False);

    EGLDisplay edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    printf("eglGetDisplay(EGL_DEFAULT_DISPLAY)=%p err=0x%x\n", (void*)edpy, eglGetError());
    EGLint major, minor;
    EGLBoolean ok = eglInitialize(edpy, &major, &minor);
    printf("eglInitialize=%d (%d.%d) err=0x%x\n", ok, major, minor, eglGetError());

    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE, 0, 0, 0};
    EGLConfig configs[64];
    EGLint ncfg = 64;
    ok = eglChooseConfig(edpy, attribs, configs, ncfg, &ncfg);
    printf("eglChooseConfig=%d ncfg=%d err=0x%x\n", ok, ncfg, eglGetError());
    EGLConfig config = configs[0];
    if (ncfg > 0) {
        EGLint r=0,g=0,b=0,a=0,vid=0;
        eglGetConfigAttrib(edpy, config, EGL_RED_SIZE, &r);
        eglGetConfigAttrib(edpy, config, EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(edpy, config, EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(edpy, config, EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(edpy, config, EGL_NATIVE_VISUAL_ID, &vid);
        printf("config0 RGBA=%d/%d/%d/%d vid=0x%x\n", r,g,b,a,vid);
    }

    EGLSurface surf = eglCreateWindowSurface(edpy, config, (EGLNativeWindowType)win, NULL);
    printf("eglCreateWindowSurface=%p err=0x%x\n", (void*)surf, eglGetError());

    ok = eglBindAPI(EGL_OPENGL_ES_API);
    printf("eglBindAPI=%d err=0x%x\n", ok, eglGetError());

    EGLint ctx_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(edpy, config, EGL_NO_CONTEXT, ctx_attribs);
    printf("eglCreateContext(MAJOR 3)=%p err=0x%x\n", (void*)ctx, eglGetError());
    if (!ctx) {
        EGLint c2[] = {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE};
        ctx = eglCreateContext(edpy, config, EGL_NO_CONTEXT, c2);
        printf("eglCreateContext(MAJOR 2)=%p err=0x%x\n", (void*)ctx, eglGetError());
    }

    if (ctx && surf) {
        ok = eglMakeCurrent(edpy, surf, surf, ctx);
        printf("eglMakeCurrent=%d err=0x%x\n", ok, eglGetError());
        if (glGetString) {
            printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
                   glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
        } else {
            printf("glGetString NULL\n");
        }
    }
    printf("DONE\n");
    return 0;
}
