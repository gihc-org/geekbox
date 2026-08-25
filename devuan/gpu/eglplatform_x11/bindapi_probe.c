/*
 * bindapi_probe.c — verificér at wrapperens eglBindAPI afviser 0x30A2 (ES2)
 * med EGL_BAD_PARAMETER (0x300C), hvilket er Firefox-mønster A's rodårsag.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef EGLBoolean (*pfn)(EGLenum);
typedef EGLint (*pfn_err)(void);

int main(void) {
    void* h = dlopen("libEGL.so.1", RTLD_LAZY);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    pfn bind = (pfn)dlsym(h, "eglBindAPI");
    pfn_err err = (pfn_err)dlsym(h, "eglGetError");
    printf("eglBindAPI=%p eglGetError=%p\n", (void*)bind, (void*)err);
    EGLenum apis[] = {0x30A0, 0x30A1, 0x30A2, 0x30A3};
    const char* names[] = {"OPENGL_API", "OPENGL_ES_API", "OPENGL_ES2_API", "OPENGL_ES3_API"};
    for (int i = 0; i < 4; i++) {
        EGLBoolean ok = bind(apis[i]);
        printf("eglBindAPI(%s=0x%x) => %d err=0x%x\n",
               names[i], apis[i], ok, (unsigned)err());
    }
    return 0;
}
