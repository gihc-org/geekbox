// dlopen_egl_test.cpp — ren glxtest-klon: dlopen libEGL → dlsym eglGetDisplay
// → kald. Isolerer hvorfor Firefox' probe får "libEGL no display", mens vores
// linkede probe virker (eglGetDisplay returnerer 0x1).
#include <dlfcn.h>
#include <EGL/egl.h>
#include <stdio.h>

typedef EGLDisplay (*GetDisplayFn)(EGLNativeDisplayType);
typedef EGLint (*GetErrorFn)(void);

int main(void)
{
    void *h = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    printf("dlopen(libEGL.so.1, NOW|GLOBAL)=%p\n", h);
    if (!h) { printf("  fejl: %s\n", dlerror()); return 1; }
    dlerror();
    GetDisplayFn eglGetDisplay = (GetDisplayFn)dlsym(h, "eglGetDisplay");
    printf("dlsym(eglGetDisplay)=%p fejl=%s\n", (void *)eglGetDisplay,
           dlerror() ? dlerror() : "(ingen)");
    GetErrorFn eglGetError = (GetErrorFn)dlsym(h, "eglGetError");
    EGLDisplay d = eglGetDisplay ? eglGetDisplay(EGL_DEFAULT_DISPLAY) : EGL_NO_DISPLAY;
    printf("eglGetDisplay(EGL_DEFAULT_DISPLAY)=%p\n", (void *)d);
    if (eglGetError)
        printf("eglGetError()=0x%x\n", eglGetError());
    if (d != EGL_NO_DISPLAY) {
        typedef int (*InitFn)(EGLDisplay, EGLint *, EGLint *);
        InitFn eglInitialize = (InitFn)dlsym(h, "eglInitialize");
        EGLint maj = 0, min = 0;
        int ok = eglInitialize ? eglInitialize(d, &maj, &min) : 0;
        printf("eglInitialize=%d (%d.%d)\n", ok, maj, min);
    }
    return 0;
}
