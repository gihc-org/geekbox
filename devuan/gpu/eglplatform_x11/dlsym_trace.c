// dlsym_trace.c — LD_PRELOAD-tracer: logger alle dlsym-opslag med "egl" i
// navnet. Bruges til at finde ud af, hvad Firefox' glxtest-probe kræver af
// vores libEGL (måling, ikke gæt).
//
// Byg:  gcc -O2 -fPIC -shared -o dlsym_trace.so dlsym_trace.c -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static void *(*real_dlsym)(void *, const char *);
static void *(*real_dlopen)(const char *, int);

static void __attribute__((constructor)) init(void)
{
    /* glibc 2.34+ har dlsym i libc; prøv kendte versioner */
    real_dlsym = (void *(*)(void *, const char *))
        dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
    if (!real_dlsym)
        real_dlsym = (void *(*)(void *, const char *))
            dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    if (!real_dlsym)
        fprintf(stderr, "[dlsym-trace] kunne ikke finde ægte dlsym\n");
    real_dlopen = (void *(*)(const char *, int))
        dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.34");
    if (!real_dlopen)
        real_dlopen = (void *(*)(const char *, int))
            dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.2.5");
    if (!real_dlopen)
        fprintf(stderr, "[dlsym-trace] kunne ikke finde ægte dlopen\n");
}

void *dlsym(void *handle, const char *symbol)
{
    void *r = real_dlsym ? real_dlsym(handle, symbol) : NULL;
    if (symbol && (strstr(symbol, "egl") || strstr(symbol, "EGL")))
        fprintf(stderr, "[dlsym-trace] dlsym(h=%p) %s -> %p\n", handle, symbol, r);
    return r;
}

void *dlopen(const char *file, int mode)
{
    void *r = real_dlopen ? real_dlopen(file, mode) : NULL;
    if (file && (strstr(file, "EGL") || strstr(file, "GLES") || strstr(file, "GL"))) {
        fprintf(stderr, "[dlsym-trace] dlopen(%s, mode=%x) -> %p", file, mode, r);
        if (!r) {
            const char *e = dlerror();
            fprintf(stderr, "; fejl: %s", e ? e : "(ingen dlerror)");
        }
        fprintf(stderr, "\n");
    }
    return r;
}
