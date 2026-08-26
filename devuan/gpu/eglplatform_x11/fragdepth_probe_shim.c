/* fragdepth_probe_shim.c — diagnose-shim til Firefox' GPU-proces.
 *
 * Formål (26. aug 2026): afgør om Subway Surfers (poki.com/en/g/subway-surfers)
 * får en WebGL1- (EGL version 2) eller WebGL2-kontekst (EGL version 3), og log
 * de shader-kilder spillet sender — for at se om en shader-omskrivning
 * (strip "GL_EXT_frag_depth"-direktivet + gl_FragDepthEXT→gl_FragDepth) kan
 * få spillet til at kompilere.
 *
 * Byg på boksen (armhf):
 *   gcc -shared -fPIC -o /usr/local/lib/firefox-webgl/fragdepth_probe.so \
 *       fragdepth_probe_shim.c -ldl
 * Kør Firefox med shimen i LD_PRELOAD (sidst), log i /tmp/fragdepth_probe.log.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static FILE *logfile(void)
{
    static FILE *f;
    if (!f) {
        f = fopen("/tmp/fragdepth_probe.log", "a");
        if (!f) f = stderr;
    }
    return f;
}

typedef EGLContext (*real_eglCreateContext_t)(EGLDisplay, EGLConfig, EGLContext,
                                              const EGLint *);

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share,
                            const EGLint *attr)
{
    static real_eglCreateContext_t real;
    if (!real) real = (real_eglCreateContext_t)dlsym(RTLD_NEXT, "eglCreateContext");
    int ver = 0;
    for (const EGLint *a = attr; a && *a != EGL_NONE; a += 2) {
        if (a[0] == EGL_CONTEXT_CLIENT_VERSION) ver = a[1];
    }
    fprintf(logfile(), "eglCreateContext version=%d\n", ver);
    fflush(logfile());
    return real(dpy, cfg, share, attr);
}

typedef void (*real_glShaderSource_t)(GLuint, GLsizei, const GLchar **,
                                      const GLint *);

void glShaderSource(GLuint shader, GLsizei count, const GLchar **str,
                    const GLint *len)
{
    static real_glShaderSource_t real;
    if (!real) real = (real_glShaderSource_t)dlsym(RTLD_NEXT, "glShaderSource");
    FILE *f = logfile();
    for (GLsizei i = 0; i < count && str && str[i]; i++) {
        fprintf(f, "--- shader %u[%d]: %.4000s\n", shader, i, str[i]);
    }
    fflush(f);
    real(shader, count, str, len);
}
