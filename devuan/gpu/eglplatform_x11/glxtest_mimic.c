#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void *(*eglGetDisplay_t)(void *);

int main(void)
{
    void *h = dlopen("libEGL.so.1", RTLD_LAZY);
    const char *e = dlerror();
    printf("dlopen(libEGL.so.1)=%p dlerror=%s\n", h, e ? e : "(none)");
    if (!h)
        return 1;

    void *sym = dlsym(h, "eglGetDisplay");
    printf("dlsym(eglGetDisplay)=%p\n", sym);
    void *sym_def = dlsym(RTLD_DEFAULT, "eglGetDisplay");
    printf("dlsym(RTLD_DEFAULT, eglGetDisplay)=%p\n", sym_def);
    void *ep = dlsym(h, "eglGetProcAddress");
    printf("dlsym(eglGetProcAddress)=%p\n", ep);

    eglGetDisplay_t f = (eglGetDisplay_t)sym;
    if (!f)
        return 2;

    void *d0 = f((void *)0);
    printf("eglGetDisplay(EGL_DEFAULT_DISPLAY=0)=%p\n", d0);
    void *d1 = f((void *)0x1234);
    printf("eglGetDisplay(0x1234)=%p\n", d1);

    printf("== maps (EGL/GLES/system) ==\n");
    FILE *m = fopen("/proc/self/maps", "r");
    char line[512];
    while (m && fgets(line, sizeof(line), m)) {
        if (strstr(line, "libEGL") || strstr(line, "libGLES") ||
            strstr(line, "system/lib") || strstr(line, "eglplatform_x11") ||
            strstr(line, "libhybris"))
            fputs(line, stdout);
    }
    if (m)
        fclose(m);
    return 0;
}
