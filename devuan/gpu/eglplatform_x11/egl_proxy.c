/* egl_proxy.c — tynd proxy for /opt/hybris/libEGL.so.1.0.0.
 *
 * Firefox dlsym'er EGL-funktionerne direkte fra libEGL-handlen (derfor virker
 * LD_PRELOAD ikke). Denne proxy ERSTATTTER hybris-libbet: den eksporterer kun
 * de funktioner vi vil huke og linker originalen (omdøbt libEGL_r.so) så alle
 * andre EGL-funktioner resolveres derfra. Vigtigt: Firefox henter GLES-funktioner
 * (fx glShaderSource) via eglGetProcAddress — derfor hukes DEN, ikke libGLESv2.
 *
 * Hooks:
 *   - eglCreateContext  → log EGL_CONTEXT_CLIENT_VERSION
 *   - eglGetProcAddress → glShaderSource: strip GL_EXT_frag_depth-direktiv +
 *                         gl_FragDepthEXT→gl_FragDepth; glCompileShader: log fejl
 *                         glDrawElements/glDrawArrays/instanced + glUseProgram/
 *                         glLinkProgram/glViewport/glClear: cyan-draw-dump
 *
 * Byg på boksen:
 *   gcc -shared -fPIC -Wl,-soname,libEGL.so.1 -o /opt/hybris/libEGL.so.1.0.0 \
 *       egl_proxy.c -L/opt/hybris -l:libEGL_r.so -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static FILE *plog(void)
{
    return fopen("/tmp/fragdepth_probe.log", "a");
}

typedef EGLContext (*real_eglCreateContext_t)(EGLDisplay, EGLConfig, EGLContext,
                                              const EGLint *);

typedef EGLBoolean (*real_eglMakeCurrent_t)(EGLDisplay, EGLSurface, EGLSurface,
                                            EGLContext);
typedef EGLBoolean (*real_eglSwapBuffers_t)(EGLDisplay, EGLSurface);
typedef EGLint (*real_eglGetError_t)(void);

static void cyan_swap_sample(void);
static void cyan_postdraw_sample(const char *tag, GLuint prog, GLsizei count);
static void cyan_predraw_capture(GLsizei count);
static void cyan_shadow_probe(const char *fn, GLenum mode, GLsizei count,
                              GLenum type, const void *indices, GLint first,
                              GLsizei primcount);
static void snapshot_vertex_buffers(GLuint prog, unsigned long seq,
                                    GLsizei count);

/* 26. aug 2026: hybris' _eglXXX-funktionstabel i libEGL_r.so er stort set
 * TOM (kun et par slots udfyldt) på denne boks → eglDestroySurface/-
 * eglDestroyContext kalder en NULL-pointer → GPU-processen crasher (ip=0x0,
 * målt 19:14 og 19:24). Udfyld alle tomme slots med de ægte Android-libEGL-
 * funktioner via android_dlopen/android_dlsym (libhybris-common). */
static void fix_egl_table(void)
{
    static const struct { const char *name; unsigned off; } tab[] = {
        { "eglGetError", 0xe498 }, { "eglGetDisplay", 0xe49c },
        { "eglInitialize", 0xe4a0 }, { "eglTerminate", 0xe4a4 },
        { "eglQueryString", 0xe4a8 }, { "eglGetConfigs", 0xe4ac },
        { "eglChooseConfig", 0xe4b0 }, { "eglGetConfigAttrib", 0xe4b4 },
        { "eglCreateWindowSurface", 0xe4b8 },
        { "eglCreatePbufferSurface", 0xe4bc },
        { "eglCreatePixmapSurface", 0xe4c0 },
        { "eglDestroySurface", 0xe4c4 }, { "eglQuerySurface", 0xe4c8 },
        { "eglBindAPI", 0xe4cc }, { "eglQueryAPI", 0xe4d0 },
        { "eglWaitClient", 0xe4d4 }, { "eglReleaseThread", 0xe4d8 },
        { "eglCreatePbufferFromClientBuffer", 0xe4dc },
        { "eglSurfaceAttrib", 0xe4e0 }, { "eglBindTexImage", 0xe4e4 },
        { "eglReleaseTexImage", 0xe4e8 }, { "eglSwapInterval", 0xe4ec },
        { "eglCreateContext", 0xe4f0 }, { "eglDestroyContext", 0xe4f4 },
        { "eglMakeCurrent", 0xe4f8 }, { "eglGetCurrentContext", 0xe4fc },
        { "eglGetCurrentSurface", 0xe500 }, { "eglGetCurrentDisplay", 0xe504 },
        { "eglQueryContext", 0xe508 }, { "eglWaitGL", 0xe50c },
        { "eglWaitNative", 0xe510 }, { "eglSwapBuffers", 0xe514 },
        { "eglCopyBuffers", 0xe518 }, { "eglCreateImageKHR", 0xe51c },
        { "eglDestroyImageKHR", 0xe520 }, { NULL, 0 }
    };
    void *(*adlopen)(const char *, int) =
        (void *(*)(const char *, int))dlsym(RTLD_DEFAULT, "android_dlopen");
    void *(*adlsym)(void *, const char *) =
        (void *(*)(void *, const char *))dlsym(RTLD_DEFAULT, "android_dlsym");
    if (!adlopen || !adlsym)
        return;
    Dl_info info;
    void *egl_wrap = dlsym(RTLD_NEXT, "eglDestroySurface");
    if (!egl_wrap || !dladdr(egl_wrap, &info) || !info.dli_fbase)
        return;
    unsigned char *base = (unsigned char *)info.dli_fbase;
    void *ah = adlopen("libEGL.so", 0);
    if (!ah)
        return;
    int fixed = 0;
    for (int i = 0; tab[i].name; i++) {
        void **slot = (void **)(base + tab[i].off);
        if (!*slot) {
            void *fn = adlsym(ah, tab[i].name);
            if (fn) {
                *slot = fn;
                fixed++;
            }
        }
    }
    fprintf(stderr, "egl_proxy: fix_egl_table udfyldte %d tomme slots "
            "(libEGL_r base=%p)\n", fixed, (void *)base);
}

static void fix_egl_table_once(void)
{
    static int done = 0;
    if (!done) {
        done = 1;
        fix_egl_table();
    }
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share,
                            const EGLint *attr)
{
    static real_eglCreateContext_t real;
    if (!real) real = (real_eglCreateContext_t)dlsym(RTLD_NEXT, "eglCreateContext");
    fix_egl_table_once();
    int ver = 0;
    for (const EGLint *a = attr; a && *a != EGL_NONE; a += 2) {
        if (a[0] == EGL_CONTEXT_CLIENT_VERSION) ver = a[1];
    }
    FILE *f = plog();
    if (f) {
        fprintf(f, "eglCreateContext version=%d\n", ver);
        fclose(f);
    }
    return real(dpy, cfg, share, attr);
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx)
{
    static real_eglMakeCurrent_t real;
    static real_eglGetError_t err;
    static long n;
    if (!real) {
        real = (real_eglMakeCurrent_t)dlsym(RTLD_NEXT, "eglMakeCurrent");
        err = (real_eglGetError_t)dlsym(RTLD_NEXT, "eglGetError");
    }
    EGLBoolean rc = real(dpy, draw, read, ctx);
    EGLint e = err ? err() : EGL_SUCCESS;
    n++;
    if (n <= 5 || n % 100 == 0) {
        FILE *f = plog();
        if (f) {
            fprintf(f, "eglMakeCurrent #%ld dpy=%p draw=%p read=%p ctx=%p rc=%d err=0x%x\n",
                    n, (void *)dpy, (void *)draw, (void *)read, (void *)ctx, (int)rc,
                    (unsigned)e);
            fclose(f);
        }
    }
    return rc;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    static real_eglSwapBuffers_t real;
    static real_eglGetError_t err;
    static long n;
    if (!real) {
        real = (real_eglSwapBuffers_t)dlsym(RTLD_NEXT, "eglSwapBuffers");
        err = (real_eglGetError_t)dlsym(RTLD_NEXT, "eglGetError");
    }
    cyan_swap_sample();
    EGLBoolean rc = real(dpy, surface);
    EGLint e = err ? err() : EGL_SUCCESS;
    n++;
    if (n <= 5 || n % 100 == 0) {
        FILE *f = plog();
        if (f) {
            fprintf(f, "eglSwapBuffers #%ld dpy=%p surf=%p rc=%d err=0x%x\n",
                    n, (void *)dpy, (void *)surface, (int)rc, (unsigned)e);
            fclose(f);
        }
    }
    return rc;
}

/* ---- shader-omskrivning (glShaderSource via eglGetProcAddress) ---- */

static void (*real_glShaderSource)(GLuint, GLsizei, const GLchar **, const GLint *);
static void (*real_glCompileShader)(GLuint);
static void (*real_glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*real_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static __eglMustCastToProperFunctionPointerType (*real_eglGetProcAddress_fn)(const char *);

/* shader-id -> GL_SHADER_TYPE (registreres ved compile; driveren returnerer 0
 * når shaderen er slettet efter link) */
static GLenum g_sh_types[8192];

static char *str_replace_all(const char *in, const char *from, const char *to)
{
    size_t fl = strlen(from), tl = strlen(to);
    size_t cap = strlen(in) + 64, outlen = 0;
    char *out = malloc(cap);
    const char *p = in;
    while (*p) {
        if (!strncmp(p, from, fl)) {
            memcpy(out + outlen, to, tl);
            outlen += tl;
            p += fl;
        } else {
            out[outlen++] = *p++;
        }
        if (outlen + fl + 1 > cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
    }
    out[outlen] = 0;
    return out;
}

/* Længde-begrænset substring-søgning: WebGL kan sende shader-kilder med
 * eksplicit længde UDEN NUL-terminator (glShaderSource), og uafgrænset
 * strstr() læste forbi bufferet → GPU-processen crashede i memchr på en
 * guard-side (SEGV_ACCERR 0xf3eff000, målt 26. aug 2026 ~19:00). */
static int contains_sub(const char *s, int slen, const char *sub)
{
    size_t l = strlen(sub);
    if (slen <= 0 || (size_t)slen < l) return 0;
    for (int i = 0; i + (int)l <= slen; i++) {
        if (!memcmp(s + i, sub, l)) return 1;
    }
    return 0;
}

/* 1.5-kompileren kan ikke heltals-varyings (målt 26. aug: ivec2/int varying →
   "Compile failed."). cs_blur's vSupport omskrives til vec2 + int()-casts. */
static char *fix_vsupport(const char *src)
{
    static const char *rules[][2] = {
        { "flat varying mediump ivec2 vSupport;",
          "flat varying mediump vec2 vSupport;" },
        { "vSupport.x = int(ceil(1.5 * blur_task.blur_radius)) * 2;",
          "vSupport.x = float(int(ceil(1.5 * blur_task.blur_radius)) * 2);" },
        { "if (vSupport.x > 0)", "if (int(vSupport.x) > 0)" },
        { "int support = min(vSupport.x, 300);",
          "int support = min(int(vSupport.x), 300);" },
        { "i <= vSupport.x", "i <= int(vSupport.x)" },
        { NULL, NULL }
    };
    char *cur = strdup(src);
    for (int r = 0; rules[r][0]; r++) {
        char *nxt = str_replace_all(cur, rules[r][0], rules[r][1]);
        free(cur);
        cur = nxt;
    }
    return cur;
}

void hook_glShaderSource(GLuint shader, GLsizei count, const GLchar **str,
                         const GLint *len)
{
    if (!real_glShaderSource)
        real_glShaderSource = (void (*)(GLuint, GLsizei, const GLchar **, const GLint *))
            (void (*)(GLuint, GLsizei, const GLchar **, const GLint *))
            real_eglGetProcAddress_fn("glShaderSource");

    FILE *f = fopen("/tmp/fragdepth_probe.log", "a");
    for (GLsizei i = 0; i < count && str && str[i]; i++) {
        int sl = (len && len[i] >= 0) ? len[i] : (int)strlen(str[i]);
        fprintf(f, "=== shader %u[%d] SRC len=%d\n", shader, i, sl);
        fwrite(str[i], 1, (size_t)(sl < 20000 ? sl : 20000), f);
        fputc('\n', f);
        char path[128];
        snprintf(path, sizeof path, "/tmp/shaders/%u_%d.glsl", shader, i);
        FILE *sf = fopen(path, "w");
        if (sf) {
            fwrite(str[i], 1, sl, sf);
            fclose(sf);
        }
    }
    fflush(f);

    int changed = 0;
    for (GLsizei i = 0; i < count && str && str[i]; i++) {
        int sl = (len && len[i] >= 0) ? len[i] : (int)strlen(str[i]);
        if (contains_sub(str[i], sl, "GL_EXT_frag_depth") ||
            contains_sub(str[i], sl, "gl_FragDepthEXT") ||
            contains_sub(str[i], sl, "vSupport")) {
            changed = 1;
            break;
        }
    }
    if (!changed) {
        if (f) fclose(f);
        real_glShaderSource(shader, count, str, len);
        return;
    }

    GLchar **newstr = calloc(count ? count : 1, sizeof(GLchar *));
    GLint *newlen = calloc(count ? count : 1, sizeof(GLint));
    for (GLsizei i = 0; i < count; i++) {
        const GLchar *s = str[i];
        int slen = (len && len[i] >= 0) ? len[i] : (int)strlen(s);
        GLchar *out = malloc(slen + 1);
        int o = 0, j = 0;
        while (j < slen) {
            if ((j == 0 || s[j - 1] == '\n') &&
                !strncmp(s + j, "#extension", 10) &&
                contains_sub(s + j, slen - j, "frag_depth")) {
                while (j < slen && s[j] != '\n') j++;
                continue;
            }
            if (!strncmp(s + j, "gl_FragDepthEXT", 15)) {
                memcpy(out + o, "gl_FragDepth", 12);
                o += 12;
                j += 15;
                continue;
            }
            out[o++] = s[j++];
        }
        out[o] = 0;
        if (strstr(s, "vSupport")) {
            char *fixed = fix_vsupport(out);
            free(out);
            out = fixed;
            o = strlen(out);
        }
        newstr[i] = out;
        newlen[i] = o;
        fprintf(f, "--- shader %u[%d] REWRITTEN: %s\n", shader, i, out);
    }
    if (f) fclose(f);
    real_glShaderSource(shader, count, (const GLchar **)newstr, newlen);
    /* Læk bevidst: driveren kan holde pointerne til glCompileShader. */
}

void hook_glCompileShader(GLuint shader)
{
    if (!real_glCompileShader) {
        real_glCompileShader = (void (*)(GLuint))real_eglGetProcAddress_fn("glCompileShader");
        real_glGetShaderiv = (void (*)(GLuint, GLenum, GLint *))real_eglGetProcAddress_fn("glGetShaderiv");
        real_glGetShaderInfoLog = (void (*)(GLuint, GLsizei, GLsizei *, GLchar *))real_eglGetProcAddress_fn("glGetShaderInfoLog");
    }
    real_glCompileShader(shader);
    GLint st = 0;
    real_glGetShaderiv(shader, GL_SHADER_TYPE, &st);
    if (shader < 8192)
        g_sh_types[shader] = (GLenum)st;
    GLint ok = 0;
    real_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        real_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            GLchar *buf = malloc(len);
            GLsizei n = 0;
            real_glGetShaderInfoLog(shader, len, &n, buf);
            FILE *f = fopen("/tmp/fragdepth_probe.log", "a");
            if (f) {
                fprintf(f, "!!! shader %u COMPILE_FAIL: %s\n", shader, buf);
                fclose(f);
            }
            free(buf);
        }
    }
}

static void (*real_glGetError_fn)(void);

GLenum hook_glGetError(void)
{
    if (!real_glGetError_fn)
        real_glGetError_fn = (void (*)(void))
            real_eglGetProcAddress_fn("glGetError");
    GLenum e = real_glGetError_fn ? ((GLenum (*)(void))real_glGetError_fn)() : 0;
    if (e != GL_NO_ERROR) {
        FILE *f = fopen("/tmp/fragdepth_probe.log", "a");
        if (f) {
            fprintf(f, "!!! glGetError -> 0x%04x\n", (unsigned)e);
            fclose(f);
        }
    }
    return e;
}

/* ======================================================================
 * Cyan-scene-instrumentering (27. aug 2026): fang draw-kald med program-,
 * shader-, uniform- og attribut-tilstand direkte i GL-kaldene (via
 * eglGetProcAddress-hooks). Formål: find hvorfor Subway Surfers' 3D-scene
 * (kun 18-verts-draws + ensartet cyan) ikke tegner objekter på 1.5-stakken.
 *   - Log: /tmp/cyan_draw_probe.log  (shader-kilder: /tmp/shaders/<id>_<i>.glsl)
 *   - Fuld dump: de første 150 draws, 18-count-draws pr. program (max 10),
 *     og store draws (>=512 verts, max 40). Ellers kompakt linje hver 250.
 * ==================================================================== */
#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#endif
#ifndef GL_TEXTURE_BINDING_CUBE_MAP
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#endif
#ifndef GL_FLOAT_MAT2x3
#define GL_FLOAT_MAT2x3 0x8B65
#endif
#ifndef GL_FLOAT_MAT2x4
#define GL_FLOAT_MAT2x4 0x8B66
#endif
#ifndef GL_FLOAT_MAT3x2
#define GL_FLOAT_MAT3x2 0x8B67
#endif
#ifndef GL_FLOAT_MAT3x4
#define GL_FLOAT_MAT3x4 0x8B68
#endif
#ifndef GL_FLOAT_MAT4x2
#define GL_FLOAT_MAT4x2 0x8B69
#endif
#ifndef GL_FLOAT_MAT4x3
#define GL_FLOAT_MAT4x3 0x8B6A
#endif
#ifndef GL_DRAW_BUFFER0
#define GL_DRAW_BUFFER0 0x8825
#endif
#ifndef GL_DRAW_BUFFER1
#define GL_DRAW_BUFFER1 0x8826
#endif
#ifndef GL_DRAW_BUFFER2
#define GL_DRAW_BUFFER2 0x8827
#endif
#ifndef GL_DRAW_BUFFER3
#define GL_DRAW_BUFFER3 0x8828
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_DIVISOR
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD 0x8C89
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

static int dlog_fd(void)
{
    static int fd = -1;
    if (fd < 0)
        fd = open("/tmp/cyan_draw_probe.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);
    return fd;
}

static void dlog_line(const char *line)
{
    int fd = dlog_fd();
    if (fd < 0)
        return;
    char buf[16384];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(buf, sizeof buf, "%lld.%03lld pid=%d tid=%ld %s\n",
                     (long long)ts.tv_sec, (long long)(ts.tv_nsec / 1000000),
                     (int)getpid(), (long)syscall(SYS_gettid), line);
    if (n > 0 && n < (int)sizeof buf)
        (void)write(fd, buf, (size_t)n);
}

static void dlog_msg(const char *fmt, ...)
{
    char line[16384];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof line)
        n = (int)sizeof line - 1;
    line[n] = 0;
    dlog_line(line);
}

/* ---- rigtige GL-funktioner (resolveres via real eglGetProcAddress) ---- */
static void (*rgl_glDrawElements)(GLenum, GLsizei, GLenum, const void *);
static void (*rgl_glDrawArrays)(GLenum, GLint, GLsizei);
static void (*rgl_glDrawElementsInstanced)(GLenum, GLsizei, GLenum,
                                           const void *, GLsizei);
static void (*rgl_glDrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
static void (*rgl_glLinkProgram)(GLuint);
static void (*rgl_glUseProgram)(GLuint);
static void (*rgl_glViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*rgl_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*rgl_glClear)(GLbitfield);
static void (*rgl_glGetAttachedShaders)(GLuint, GLsizei, GLsizei *, GLuint *);
static void (*rgl_glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*rgl_glGetProgramiv)(GLuint, GLenum, GLint *);
static void (*rgl_glGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei *,
                                      GLint *, GLenum *, GLchar *);
static GLint (*rgl_glGetUniformLocation)(GLuint, const GLchar *);
static void (*rgl_glGetUniformfv)(GLuint, GLint, GLfloat *);
static void (*rgl_glGetUniformiv)(GLuint, GLint, GLint *);
static void (*rgl_glGetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei *,
                                     GLint *, GLenum *, GLchar *);
static GLint (*rgl_glGetAttribLocation)(GLuint, const GLchar *);
static void (*rgl_glGetVertexAttribiv)(GLuint, GLenum, GLint *);
static void (*rgl_glGetVertexAttribfv)(GLuint, GLenum, GLfloat *);
static void (*rgl_glGetVertexAttribPointerv)(GLuint, GLenum, void **);
static void (*rgl_glGetIntegerv)(GLenum, GLint *);
static GLboolean (*rgl_glIsEnabled)(GLenum);
static void (*rgl_glBindFramebuffer)(GLenum, GLuint);
static void (*rgl_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint,
                                          GLint);
static void (*rgl_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
static void (*rgl_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                GLint, GLenum, GLenum, const void *);
static void (*rgl_glTexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static void (*rgl_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum,
                                GLenum, void *);
static void (*rgl_glGetFloatv)(GLenum, GLfloat *);
static void (*rgl_glGetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *);
static void (*rgl_glBindBuffer)(GLenum, GLuint);
static void (*rgl_glGetBufferParameteriv)(GLenum, GLenum, GLint *);
static void *(*rgl_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr,
                                     GLbitfield);
static GLboolean (*rgl_glUnmapBuffer)(GLenum);
static void (*rgl_glVertexAttribDivisor)(GLuint, GLuint);
static void (*rgl_glDrawBuffers)(GLsizei, const GLenum *);
static void (*rgl_glEnable)(GLenum);
static void (*rgl_glDisable)(GLenum);
static void (*rgl_glDepthMask)(GLboolean);

static void rgl_resolve_all(void)
{
    if (rgl_glDrawElements)
        return;
    rgl_glDrawElements = (void (*)(GLenum, GLsizei, GLenum, const void *))
        real_eglGetProcAddress_fn("glDrawElements");
    rgl_glDrawArrays = (void (*)(GLenum, GLint, GLsizei))
        real_eglGetProcAddress_fn("glDrawArrays");
    rgl_glDrawElementsInstanced =
        (void (*)(GLenum, GLsizei, GLenum, const void *, GLsizei))
        real_eglGetProcAddress_fn("glDrawElementsInstanced");
    rgl_glDrawArraysInstanced = (void (*)(GLenum, GLint, GLsizei, GLsizei))
        real_eglGetProcAddress_fn("glDrawArraysInstanced");
    rgl_glLinkProgram = (void (*)(GLuint))
        real_eglGetProcAddress_fn("glLinkProgram");
    rgl_glUseProgram = (void (*)(GLuint))
        real_eglGetProcAddress_fn("glUseProgram");
    rgl_glViewport = (void (*)(GLint, GLint, GLsizei, GLsizei))
        real_eglGetProcAddress_fn("glViewport");
    rgl_glClearColor = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))
        real_eglGetProcAddress_fn("glClearColor");
    rgl_glClear = (void (*)(GLbitfield))
        real_eglGetProcAddress_fn("glClear");
    rgl_glGetAttachedShaders = (void (*)(GLuint, GLsizei, GLsizei *, GLuint *))
        real_eglGetProcAddress_fn("glGetAttachedShaders");
    rgl_glGetShaderiv = (void (*)(GLuint, GLenum, GLint *))
        real_eglGetProcAddress_fn("glGetShaderiv");
    rgl_glGetProgramiv = (void (*)(GLuint, GLenum, GLint *))
        real_eglGetProcAddress_fn("glGetProgramiv");
    rgl_glGetActiveUniform = (void (*)(GLuint, GLuint, GLsizei, GLsizei *,
                                       GLint *, GLenum *, GLchar *))
        real_eglGetProcAddress_fn("glGetActiveUniform");
    rgl_glGetUniformLocation = (GLint (*)(GLuint, const GLchar *))
        real_eglGetProcAddress_fn("glGetUniformLocation");
    rgl_glGetUniformfv = (void (*)(GLuint, GLint, GLfloat *))
        real_eglGetProcAddress_fn("glGetUniformfv");
    rgl_glGetUniformiv = (void (*)(GLuint, GLint, GLint *))
        real_eglGetProcAddress_fn("glGetUniformiv");
    rgl_glGetActiveAttrib = (void (*)(GLuint, GLuint, GLsizei, GLsizei *,
                                      GLint *, GLenum *, GLchar *))
        real_eglGetProcAddress_fn("glGetActiveAttrib");
    rgl_glGetAttribLocation = (GLint (*)(GLuint, const GLchar *))
        real_eglGetProcAddress_fn("glGetAttribLocation");
    rgl_glGetVertexAttribiv = (void (*)(GLuint, GLenum, GLint *))
        real_eglGetProcAddress_fn("glGetVertexAttribiv");
    rgl_glGetVertexAttribfv = (void (*)(GLuint, GLenum, GLfloat *))
        real_eglGetProcAddress_fn("glGetVertexAttribfv");
    rgl_glGetVertexAttribPointerv = (void (*)(GLuint, GLenum, void **))
        real_eglGetProcAddress_fn("glGetVertexAttribPointerv");
    rgl_glGetIntegerv = (void (*)(GLenum, GLint *))
        real_eglGetProcAddress_fn("glGetIntegerv");
    rgl_glIsEnabled = (GLboolean (*)(GLenum))
        real_eglGetProcAddress_fn("glIsEnabled");
    rgl_glBindFramebuffer = (void (*)(GLenum, GLuint))
        real_eglGetProcAddress_fn("glBindFramebuffer");
    rgl_glFramebufferTexture2D = (void (*)(GLenum, GLenum, GLenum, GLuint,
                                           GLint))
        real_eglGetProcAddress_fn("glFramebufferTexture2D");
    rgl_glFramebufferRenderbuffer = (void (*)(GLenum, GLenum, GLenum, GLuint))
        real_eglGetProcAddress_fn("glFramebufferRenderbuffer");
    rgl_glTexImage2D = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                 GLint, GLenum, GLenum, const void *))
        real_eglGetProcAddress_fn("glTexImage2D");
    rgl_glTexStorage2D = (void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei))
        real_eglGetProcAddress_fn("glTexStorage2D");
    rgl_glReadPixels = (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum,
                                 GLenum, void *))
        real_eglGetProcAddress_fn("glReadPixels");
    rgl_glGetFloatv = (void (*)(GLenum, GLfloat *))
        real_eglGetProcAddress_fn("glGetFloatv");
    rgl_glGetBufferSubData = (void (*)(GLenum, GLintptr, GLsizeiptr, void *))
        real_eglGetProcAddress_fn("glGetBufferSubData");
    rgl_glBindBuffer = (void (*)(GLenum, GLuint))
        real_eglGetProcAddress_fn("glBindBuffer");
    rgl_glGetBufferParameteriv = (void (*)(GLenum, GLenum, GLint *))
        real_eglGetProcAddress_fn("glGetBufferParameteriv");
    rgl_glMapBufferRange = (void *(*)(GLenum, GLintptr, GLsizeiptr,
                                      GLbitfield))
        real_eglGetProcAddress_fn("glMapBufferRange");
    rgl_glUnmapBuffer = (GLboolean (*)(GLenum))
        real_eglGetProcAddress_fn("glUnmapBuffer");
    rgl_glVertexAttribDivisor = (void (*)(GLuint, GLuint))
        real_eglGetProcAddress_fn("glVertexAttribDivisor");
    rgl_glDrawBuffers = (void (*)(GLsizei, const GLenum *))
        real_eglGetProcAddress_fn("glDrawBuffers");
    rgl_glEnable = (void (*)(GLenum))
        real_eglGetProcAddress_fn("glEnable");
    rgl_glDisable = (void (*)(GLenum))
        real_eglGetProcAddress_fn("glDisable");
    rgl_glDepthMask = (void (*)(GLboolean))
        real_eglGetProcAddress_fn("glDepthMask");
}

/* depthmask-lap (vnext7): hvis en draw kører med DEPTH_TEST fra men
 * DEPTH_WRITEMASK=1 (2D/UI-draws i Subway Surfers), tvinges masken fra under
 * draw'et og gendannes bagefter. På 1.5-stakken skriver driveren ellers
 * depth alligevel → depth-bufferen fyldes med UI-dybdens ≈0 → efterfølgende
 * verdens-draws (LEQUAL) fejler. Målt 8. sep 2026 aften (vnext6 var-depthoff:
 * verden rasterserer live med depth fra). */
static void dm_lap_begin(int *active)
{
    *active = 0;
    if (!rgl_glDepthMask || !rgl_glIsEnabled || !rgl_glGetIntegerv)
        return;
    if (rgl_glIsEnabled(GL_DEPTH_TEST))
        return;
    GLint dm = 0;
    rgl_glGetIntegerv(GL_DEPTH_WRITEMASK, &dm);
    if (dm) {
        rgl_glDepthMask(GL_FALSE);
        *active = 1;
    }
}

static void dm_lap_end(int active)
{
    if (active && rgl_glDepthMask)
        rgl_glDepthMask(GL_TRUE);
}

/* vnext8-diagnose: hvis CYAN_DEPTHCLEAR=1, ryd depth-bufferen lige før store
 * depth-on-draws — tester om live-porten er at fbo=3's depth aldrig ryddes
 * (clear i spillet rammer måske fbo=0; UI skriver depth ≈0 med depthtest=0
 * + depthmask=1 på 1.5). */
static void maybe_force_depth_clear(GLsizei count)
{
    if (count < 512)
        return;
    if (!getenv("CYAN_DEPTHCLEAR"))
        return;
    if (!rgl_glClear || !rgl_glIsEnabled || !rgl_glGetIntegerv)
        return;
    if (!rgl_glIsEnabled(GL_DEPTH_TEST))
        return;
    GLint fbo = 0, vp[4] = {0, 0, 0, 0};
    rgl_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    rgl_glGetIntegerv(GL_VIEWPORT, vp);
    rgl_glClear(GL_DEPTH_BUFFER_BIT);
    static unsigned long n = 0;
    n++;
    if (n <= 40 || n % 250 == 0)
        dlog_msg("force-depth-clear fbo=%d %dx%d count=%d (kald=%lu)",
                 (int)fbo, vp[2], vp[3], (int)count, n);
}

static const char *gls_mode_name(GLenum m)
{
    switch (m) {
    case 0x0000: return "POINTS";
    case 0x0001: return "LINES";
    case 0x0002: return "LINE_LOOP";
    case 0x0003: return "LINE_STRIP";
    case 0x0004: return "TRIANGLES";
    case 0x0005: return "TRIANGLE_STRIP";
    case 0x0006: return "TRIANGLE_FAN";
    default: return "?";
    }
}

static const char *gls_idx_name(GLenum t)
{
    switch (t) {
    case 0x1401: return "UNSIGNED_BYTE";
    case 0x1403: return "UNSIGNED_SHORT";
    case 0x1405: return "UNSIGNED_INT";
    default: return "?";
    }
}

static const char *gls_type_name(GLenum t)
{
    switch (t) {
    case 0x1406: return "FLOAT";
    case 0x1404: return "INT";
    case 0x1405: return "UNSIGNED_INT";
    case 0x1402: return "SHORT";
    case 0x1401: return "UNSIGNED_BYTE";
    case 0x1400: return "BYTE";
    case 0x8B50: return "FLOAT_VEC2";
    case 0x8B51: return "FLOAT_VEC3";
    case 0x8B52: return "FLOAT_VEC4";
    case 0x8B53: return "INT_VEC2";
    case 0x8B54: return "INT_VEC3";
    case 0x8B55: return "INT_VEC4";
    case 0x8B56: return "BOOL";
    case 0x8B57: return "BOOL_VEC2";
    case 0x8B58: return "BOOL_VEC3";
    case 0x8B59: return "BOOL_VEC4";
    case 0x8B5A: return "FLOAT_MAT2";
    case 0x8B5B: return "FLOAT_MAT3";
    case 0x8B5C: return "FLOAT_MAT4";
    case GL_FLOAT_MAT2x3: return "FLOAT_MAT2x3";
    case GL_FLOAT_MAT2x4: return "FLOAT_MAT2x4";
    case GL_FLOAT_MAT3x2: return "FLOAT_MAT3x2";
    case GL_FLOAT_MAT3x4: return "FLOAT_MAT3x4";
    case GL_FLOAT_MAT4x2: return "FLOAT_MAT4x2";
    case GL_FLOAT_MAT4x3: return "FLOAT_MAT4x3";
    case 0x8B5E: return "SAMPLER_2D";
    case 0x8B60: return "SAMPLER_CUBE";
    case 0x8B5F: return "SAMPLER_3D";
    default: return "?";
    }
}

static int gls_is_float(GLenum t)
{
    switch (t) {
    case 0x1406:
    case 0x8B50: case 0x8B51: case 0x8B52:
    case 0x8B5A: case 0x8B5B: case 0x8B5C:
    case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT2x4:
    case GL_FLOAT_MAT3x2: case GL_FLOAT_MAT3x4:
    case GL_FLOAT_MAT4x2: case GL_FLOAT_MAT4x3:
        return 1;
    default:
        return 0;
    }
}

static int gls_comp(GLenum t)
{
    switch (t) {
    case 0x8B50: case 0x8B53: case 0x8B57: return 2;
    case 0x8B51: case 0x8B54: case 0x8B58: return 3;
    case 0x8B52: case 0x8B55: case 0x8B59: return 4;
    case 0x8B5A: return 4;
    case 0x8B5B: return 9;
    case 0x8B5C: return 16;
    case GL_FLOAT_MAT2x3: return 6;
    case GL_FLOAT_MAT2x4: return 8;
    case GL_FLOAT_MAT3x2: return 6;
    case GL_FLOAT_MAT3x4: return 12;
    case GL_FLOAT_MAT4x2: return 8;
    case GL_FLOAT_MAT4x3: return 12;
    default: return 1;
    }
}

static unsigned shader_file_sig(const char *path, size_t *lenp)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        *lenp = 0;
        return 0;
    }
    unsigned h = 2166136261u;
    int c;
    size_t len = 0;
    while ((c = fgetc(f)) != EOF && len < 524288) {
        h ^= (unsigned char)c;
        h *= 16777619u;
        len++;
    }
    fclose(f);
    *lenp = len;
    return h;
}

static void shader_first_line(const char *path, char *out, size_t outsz)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    if (fgets(out, (int)outsz, f)) {
        size_t l = strlen(out);
        while (l && (out[l - 1] == '\n' || out[l - 1] == '\r'))
            out[--l] = 0;
    }
    fclose(f);
}

static void shader_desc(GLuint sh, GLenum stype, char *out, size_t outsz)
{
    if (!stype && sh < 8192)
        stype = g_sh_types[sh];
    if (!stype)
        stype = 0xffff;
    char tmp[128];
    int o = snprintf(out, outsz, "    shader id=%u type=%s",
                     (unsigned)sh, gls_type_name(stype));
    int nfiles = 0;
    size_t tot = 0;
    unsigned hash = 0;
    char first[160] = "";
    for (int i = 0; i < 8; i++) {
        snprintf(tmp, sizeof tmp, "/tmp/shaders/%u_%d.glsl", (unsigned)sh, i);
        size_t l = 0;
        unsigned h = shader_file_sig(tmp, &l);
        if (l > 0) {
            nfiles++;
            tot += l;
            hash = hash ? hash ^ (h * 33u) : h;
            if (nfiles == 1)
                shader_first_line(tmp, first, sizeof first);
        }
    }
    if (o >= 0 && o < (int)outsz)
        o += snprintf(out + o, outsz - (size_t)o,
                      " files=%d bytes=%zu fnv=%08x", nfiles, tot, hash);
    if (first[0] && o >= 0 && o < (int)outsz)
        snprintf(out + o, outsz - (size_t)o, " forste-linje: %s", first);
}

static void dump_uniforms(GLuint prog)
{
    GLint nu = 0;
    rgl_glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &nu);
    dlog_msg("  active-uniforms=%d", (int)nu);
    if (nu > 128)
        nu = 128;
    for (GLint i = 0; i < nu; i++) {
        GLchar name[160];
        GLsizei nl = 0;
        GLint sz = 0;
        GLenum ut = 0;
        rgl_glGetActiveUniform(prog, (GLuint)i, (GLsizei)sizeof name, &nl,
                               &sz, &ut, name);
        GLint loc = rgl_glGetUniformLocation(prog, name);
        if (loc < 0) {
            dlog_msg("    U[%d] '%s' type=%s size=%d loc=-1 (fjernet)",
                     (int)i, name, gls_type_name(ut), (int)sz);
            continue;
        }
        int comp = gls_comp(ut);
        int total = comp * (sz > 0 ? sz : 1);
        if (total > 32)
            total = 32;
        if (gls_is_float(ut)) {
            GLfloat fv[32];
            rgl_glGetUniformfv(prog, loc, fv);
            char line[2048];
            int o = snprintf(line, sizeof line,
                             "    U[%d] '%s' type=%s size=%d loc=%d val=[",
                             (int)i, name, gls_type_name(ut), (int)sz,
                             (int)loc);
            for (int k = 0; k < total; k++) {
                if (o >= 0 && o < (int)sizeof line)
                    o += snprintf(line + o, sizeof line - (size_t)o,
                                  "%s%.6g", k ? "," : "", fv[k]);
            }
            if (o >= 0 && o < (int)sizeof line)
                snprintf(line + o, sizeof line - (size_t)o, "]");
            dlog_line(line);
            if (ut == 0x8B5C) {
                char rl[1024];
                int ro = snprintf(rl, sizeof rl,
                                  "      mat4 kolonne-major v0..v15 = "
                                  "[%.6g,%.6g,%.6g,%.6g, %.6g,%.6g,%.6g,%.6g, "
                                  "%.6g,%.6g,%.6g,%.6g, %.6g,%.6g,%.6g,%.6g]",
                                  fv[0], fv[1], fv[2], fv[3],
                                  fv[4], fv[5], fv[6], fv[7],
                                  fv[8], fv[9], fv[10], fv[11],
                                  fv[12], fv[13], fv[14], fv[15]);
                (void)ro;
                dlog_line(rl);
                ro = snprintf(rl, sizeof rl,
                              "      mat4 rækker (række-major): "
                              "[%.6g,%.6g,%.6g,%.6g] "
                              "[%.6g,%.6g,%.6g,%.6g] "
                              "[%.6g,%.6g,%.6g,%.6g] "
                              "[%.6g,%.6g,%.6g,%.6g]",
                              fv[0], fv[4], fv[8], fv[12],
                              fv[1], fv[5], fv[9], fv[13],
                              fv[2], fv[6], fv[10], fv[14],
                              fv[3], fv[7], fv[11], fv[15]);
                (void)ro;
                dlog_line(rl);
            }
        } else {
            GLint iv[32];
            rgl_glGetUniformiv(prog, loc, iv);
            char line[2048];
            int o = snprintf(line, sizeof line,
                             "    U[%d] '%s' type=%s size=%d loc=%d val=[",
                             (int)i, name, gls_type_name(ut), (int)sz,
                             (int)loc);
            for (int k = 0; k < total; k++) {
                if (o >= 0 && o < (int)sizeof line)
                    o += snprintf(line + o, sizeof line - (size_t)o,
                                  "%s%d", k ? "," : "", iv[k]);
            }
            if (o >= 0 && o < (int)sizeof line)
                snprintf(line + o, sizeof line - (size_t)o, "]");
            dlog_line(line);
        }
    }
}

static void dump_attribs(GLuint prog)
{
    GLint na = 0;
    rgl_glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &na);
    dlog_msg("  active-attribs=%d", (int)na);
    if (na > 64)
        na = 64;
    for (GLint i = 0; i < na; i++) {
        GLchar name[128];
        GLsizei nl = 0;
        GLint sz = 0;
        GLenum at = 0;
        rgl_glGetActiveAttrib(prog, (GLuint)i, (GLsizei)sizeof name, &nl,
                              &sz, &at, name);
        GLint loc = rgl_glGetAttribLocation(prog, name);
        dlog_msg("    A[%d] '%s' loc=%d type=%s size=%d", (int)i, name,
                 (int)loc, gls_type_name(at), (int)sz);
    }
    GLint maxa = 0;
    rgl_glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxa);
    if (maxa > 16)
        maxa = 16;
    int any = 0;
    for (GLint i = 0; i < maxa; i++) {
        GLint en = 0;
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                                &en);
        if (!en)
            continue;
        any = 1;
        GLint sz = 0, str = 0, tp = 0, nrm = 0, buf = 0, divs = 0;
        void *ptr = NULL;
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &sz);
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                                &str);
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &tp);
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                                &nrm);
        rgl_glGetVertexAttribiv((GLuint)i,
                                GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buf);
        rgl_glGetVertexAttribPointerv((GLuint)i,
                                      GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr);
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_DIVISOR,
                                &divs);
        GLfloat cur[4] = {0, 0, 0, 0};
        rgl_glGetVertexAttribfv((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, cur);
        dlog_msg("    attrib-array[%d] en size=%d type=0x%x(%s) stride=%d "
                 "norm=%d buf=%d ptr=%p divisor=%d "
                 "cur=[%.4g,%.4g,%.4g,%.4g]",
                 (int)i, (int)sz, (unsigned)tp, gls_type_name((GLenum)tp),
                 (int)str, (int)nrm, (int)buf, ptr, (int)divs,
                 cur[0], cur[1], cur[2], cur[3]);
    }
    if (!any)
        dlog_msg("    (ingen vertex-attrib-arrays aktiveret)");
}

static void dump_state_extra(void)
{
    GLint vp[4] = {0, 0, 0, 0};
    GLint sc[4] = {0, 0, 0, 0};
    GLint b = 0;
    rgl_glGetIntegerv(GL_VIEWPORT, vp);
    rgl_glGetIntegerv(GL_SCISSOR_BOX, sc);
    rgl_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &b);
    dlog_msg("  viewport=[%d,%d %dx%d] scissor=[%d,%d %dx%d] fbo=%d",
             vp[0], vp[1], vp[2], vp[3], sc[0], sc[1], sc[2], sc[3], (int)b);
    GLint rfbo = 0;
    rgl_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfbo);
    GLint cmode = 0, front = 0;
    rgl_glGetIntegerv(GL_CULL_FACE_MODE, &cmode);
    rgl_glGetIntegerv(GL_FRONT_FACE, &front);
    GLint bsrc = 0, bdst = 0, bsrca = 0, bdsta = 0;
    rgl_glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc);
    rgl_glGetIntegerv(GL_BLEND_DST_RGB, &bdst);
    rgl_glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrca);
    rgl_glGetIntegerv(GL_BLEND_DST_ALPHA, &bdsta);
    GLint depthf = 0;
    rgl_glGetIntegerv(GL_DEPTH_FUNC, &depthf);
    GLint cmask[4] = {0, 0, 0, 0};
    GLint dmask = 0;
    rgl_glGetIntegerv(GL_COLOR_WRITEMASK, cmask);
    rgl_glGetIntegerv(GL_DEPTH_WRITEMASK, &dmask);
    GLint vao = 0, arrbuf = 0, ebuf = 0;
    rgl_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    rgl_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrbuf);
    rgl_glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebuf);
    dlog_msg("  state: cull=%d cullmode=0x%x(%s) front=0x%x depthtest=%d "
             "depthfunc=0x%x scissor=%d blend=%d src=0x%x dst=0x%x "
             "srca=0x%x dsta=0x%x "
             "colormask=%d%d%d%d depthmask=%d readfbo=%d",
             rgl_glIsEnabled ? rgl_glIsEnabled(GL_CULL_FACE) : -1,
             (unsigned)cmode, cmode == 0x0405 ? "BACK" :
                 (cmode == 0x0404 ? "FRONT" : "?"),
             (unsigned)front,
             rgl_glIsEnabled ? rgl_glIsEnabled(GL_DEPTH_TEST) : -1,
             (unsigned)depthf,
             rgl_glIsEnabled ? rgl_glIsEnabled(GL_SCISSOR_TEST) : -1,
             rgl_glIsEnabled ? rgl_glIsEnabled(GL_BLEND) : -1,
             (unsigned)bsrc, (unsigned)bdst, (unsigned)bsrca, (unsigned)bdsta,
             cmask[0], cmask[1], cmask[2], cmask[3], dmask, (int)rfbo);
    dlog_msg("  extra-state: rasterDiscard=%d stencilTest=%d vao=%d "
             "arrbuf=%d ebuf=%d",
             rgl_glIsEnabled ? rgl_glIsEnabled(GL_RASTERIZER_DISCARD) : -1,
             rgl_glIsEnabled ? rgl_glIsEnabled(GL_STENCIL_TEST) : -1,
             (int)vao, (int)arrbuf, (int)ebuf);
    GLint at = 0;
    rgl_glGetIntegerv(GL_ACTIVE_TEXTURE, &at);
    GLint tb2d = 0, tbc = 0;
    rgl_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tb2d);
    rgl_glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &tbc);
    dlog_msg("  tex: active=0x%x bind2d=%d bindcube=%d", (unsigned)at,
             (int)tb2d, (int)tbc);
    GLint db[4] = {-1, -1, -1, -1};
    rgl_glGetIntegerv(GL_DRAW_BUFFER0, &db[0]);
    rgl_glGetIntegerv(GL_DRAW_BUFFER1, &db[1]);
    rgl_glGetIntegerv(GL_DRAW_BUFFER2, &db[2]);
    rgl_glGetIntegerv(GL_DRAW_BUFFER3, &db[3]);
    dlog_msg("  drawBuffers=[0x%x,0x%x,0x%x,0x%x]", (unsigned)db[0],
             (unsigned)db[1], (unsigned)db[2], (unsigned)db[3]);
    GLfloat dcv = -1.0f;
    if (rgl_glGetFloatv)
        rgl_glGetFloatv(GL_DEPTH_CLEAR_VALUE, &dcv);
    dlog_msg("  depthClear=%g", (double)dcv);
}

/* Snapshot af vertex-/index-buffere via glMapBufferRange (glGetBufferSubData
 * er NULL på 1.5-stakken; map-range virker — målt 8. sep 2026). Skriv til
 * /tmp/vb_p<prog>_q<seq>.a<attrib>.bin + /tmp/eb_p<prog>_q<seq>.bin så vi
 * offline kan regne clip-space med de loggede uniform-matricer. */
static void snapshot_vertex_buffers(GLuint prog, unsigned long seq,
                                    GLsizei count)
{
    static int snaps = 0;
    if (snaps >= 32)
        return;
    if (!rgl_glMapBufferRange || !rgl_glUnmapBuffer || !rgl_glBindBuffer ||
        !rgl_glGetIntegerv || !rgl_glGetVertexAttribiv ||
        !rgl_glGetVertexAttribPointerv || !rgl_glGetBufferParameteriv)
        return;
    snaps++;
    GLint maxa = 0, ab = 0;
    rgl_glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxa);
    rgl_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &ab);
    if (maxa > 16)
        maxa = 16;
    for (GLint i = 0; i < maxa; i++) {
        GLint en = 0, sz = 0, str = 0, tp = 0, buf = 0;
        void *ptr = NULL;
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                                &en);
        if (!en)
            continue;
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &sz);
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                                &str);
        rgl_glGetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &tp);
        rgl_glGetVertexAttribiv((GLuint)i,
                                GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buf);
        rgl_glGetVertexAttribPointerv((GLuint)i,
                                      GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr);
        if (!buf || tp != GL_FLOAT || sz < 1 || sz > 4)
            continue;
        GLsizeiptr esize = (GLsizeiptr)sz * (GLsizeiptr)sizeof(GLfloat);
        GLsizeiptr bstride = str > 0 ? (GLsizeiptr)str : esize;
        if (bstride <= 0)
            continue;
        GLintptr off = (GLintptr)(uintptr_t)ptr;
        GLint bsize = 0;
        rgl_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buf);
        rgl_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bsize);
        if (off < 0 || off >= bsize) {
            rgl_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)ab);
            continue;
        }
        /* Læs hele det draw'ede vertex-område (men højst 1 MB pr. attrib). */
        GLsizeiptr want = bstride * (GLsizeiptr)count;
        GLsizeiptr len = want;
        if (len > (GLsizeiptr)bsize - off)
            len = (GLsizeiptr)bsize - off;
        if (len > 1048576)
            len = 1048576;
        void *m = rgl_glMapBufferRange(GL_ARRAY_BUFFER, off, len,
                                       GL_MAP_READ_BIT);
        if (!m) {
            rgl_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)ab);
            continue;
        }
        char path[160];
        snprintf(path, sizeof path, "/tmp/vb_p%u_q%lu_a%d.bin",
                 (unsigned)prog, seq, (int)i);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(m, 1, (size_t)len, f);
            fclose(f);
        }
        rgl_glUnmapBuffer(GL_ARRAY_BUFFER);
        dlog_msg("VBUF-SNAP p=%u q=%lu a=%d buf=%d size=%d type=0x%x "
                 "stride=%d off=%lld count=%d bytes=%lld file=%s",
                 (unsigned)prog, seq, (int)i, (int)buf, (int)sz,
                 (unsigned)tp, (int)str, (long long)off, (int)count,
                 (long long)len, path);
        rgl_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)ab);
    }
    /* Index-buffer: spillet bruger UNSIGNED_SHORT + offset 0. */
    GLint ebo = 0, esize = 0;
    rgl_glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
    if (ebo) {
        rgl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)ebo);
        rgl_glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE,
                                   &esize);
        GLsizeiptr want = (GLsizeiptr)count * 2;
        GLsizeiptr len = want < esize ? want : (GLsizeiptr)esize;
        if (len > 0) {
            void *m = rgl_glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, len,
                                           GL_MAP_READ_BIT);
            if (m) {
                char path[160];
                snprintf(path, sizeof path, "/tmp/eb_p%u_q%lu.bin",
                         (unsigned)prog, seq);
                FILE *f = fopen(path, "wb");
                if (f) {
                    fwrite(m, 1, (size_t)len, f);
                    fclose(f);
                }
                rgl_glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
                dlog_msg("EBUF-SNAP p=%u q=%lu ebo=%d count=%d bytes=%lld "
                         "file=%s", (unsigned)prog, seq, (int)ebo,
                         (int)count, (long long)len, path);
            }
        }
    }
}

static void dump_draw(const char *fn, GLenum mode, GLsizei count,
                      GLenum type, GLsizei primcount, const void *indices)
{
    static unsigned long seq = 0;
    static struct { GLuint prog; int n; } p18[64];
    static int np18 = 0;
    static int big = 0;
    static unsigned long skipped = 0;
    seq++;
    if (seq == 1)
        dlog_msg("== proxy-build: vnext9 + depthmask-lap + tvungen "
                 "depth-clear (env CYAN_DEPTHCLEAR=1) + SHADOW-probe med "
                 "var-depthoff/var-culloff til big<=40 + hver 250. store "
                 "draw (env CYAN_SHADOW_PROBE) ==");
    GLint prog = 0;
    rgl_glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    int want = (seq <= 150);
    if (!want && count == 18) {
        int found = -1;
        for (int i = 0; i < np18; i++) {
            if (p18[i].prog == (GLuint)prog) {
                found = i;
                break;
            }
        }
        if (found < 0 && np18 < 64) {
            found = np18++;
            p18[found].prog = (GLuint)prog;
            p18[found].n = 0;
        }
        if (found >= 0 && p18[found].n < 10) {
            p18[found].n++;
            want = 1;
        }
    }
    if (!want && count >= 512 && big < 40) {
        big++;
        want = 1;
    }
    if (want) {
        dlog_msg("DRAW-DUMP %s mode=0x%x(%s) count=%d type=0x%x(%s) "
                 "indices=%p primcount=%d prog=%u (draw-seq=%lu)",
                 fn, (unsigned)mode, gls_mode_name(mode), (int)count,
                 (unsigned)type, gls_idx_name(type), indices,
                 (int)primcount, (unsigned)prog, seq);
        if (!prog) {
            dlog_msg("  (ingen program bundet ved draw!)");
        } else {
            GLuint shs[8];
            GLsizei nsh = 0;
            rgl_glGetAttachedShaders((GLuint)prog, 8, &nsh, shs);
            dlog_msg("  attached-shaders n=%d", (int)nsh);
            for (GLsizei i = 0; i < nsh && i < 8; i++) {
                GLint st = 0;
                rgl_glGetShaderiv(shs[i], GL_SHADER_TYPE, &st);
                char desc[768];
                shader_desc(shs[i], (GLenum)st, desc, sizeof desc);
                dlog_line(desc);
            }
            dump_uniforms((GLuint)prog);
            dump_attribs((GLuint)prog);
        }
        dump_state_extra();
        if (count >= 512)
            snapshot_vertex_buffers((GLuint)prog, seq, count);
    } else {
        skipped++;
        if (skipped % 100 == 0) {
            GLint fbo = 0;
            rgl_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            GLint vp[4] = {0, 0, 0, 0};
            rgl_glGetIntegerv(GL_VIEWPORT, vp);
            dlog_msg("draw-kompakt %s mode=0x%x count=%d type=0x%x prog=%u "
                     "fbo=%d viewport=[%d,%d %dx%d] (draw-seq=%lu)",
                     fn, (unsigned)mode, (int)count, (unsigned)type,
                     (unsigned)prog, (int)fbo, vp[0], vp[1], vp[2], vp[3],
                     seq);
        }
    }
    /* Periodisk fuld stikprøve (hvert 500. draw) så vi ser steady-state
     * gameplay, ikke kun opstarts-bursten. */
    if (!want && seq > 150 && seq % 500 == 0) {
        GLint fbo = 0;
        rgl_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        dlog_msg("PERIODIC-PROBE seq=%lu count=%d prog=%u fbo=%d", seq,
                 (int)count, (unsigned)prog, (int)fbo);
        if (!prog) {
            dlog_msg("  (ingen program bundet ved draw!)");
        } else {
            GLuint shs[8];
            GLsizei nsh = 0;
            rgl_glGetAttachedShaders((GLuint)prog, 8, &nsh, shs);
            dlog_msg("  attached-shaders n=%d", (int)nsh);
            for (GLsizei i = 0; i < nsh && i < 8; i++) {
                GLint st = 0;
                rgl_glGetShaderiv(shs[i], GL_SHADER_TYPE, &st);
                char desc[768];
                shader_desc(shs[i], (GLenum)st, desc, sizeof desc);
                dlog_line(desc);
            }
            dump_uniforms((GLuint)prog);
            dump_attribs((GLuint)prog);
        }
        dump_state_extra();
    }
}

static void hook_glDrawElements(GLenum mode, GLsizei count, GLenum type,
                                const void *indices)
{
    rgl_resolve_all();
    dump_draw("glDrawElements", mode, count, type, -1, indices);
    cyan_predraw_capture(count);
    maybe_force_depth_clear(count);
    int dm_lap = 0;
    dm_lap_begin(&dm_lap);
    rgl_glDrawElements(mode, count, type, indices);
    dm_lap_end(dm_lap);
    if (dm_lap) {
        static unsigned long n = 0;
        n++;
        if (n <= 120 || n % 300 == 0)
            dlog_msg("depthmask-lap glDrawElements count=%d (kald=%lu)",
                     (int)count, n);
    }
    cyan_postdraw_sample("drawElements", 0, count);
    if (getenv("CYAN_SHADOW_PROBE"))
        cyan_shadow_probe("drawElements", mode, count, type, indices, 0, -1);
}

static void hook_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    rgl_resolve_all();
    dump_draw("glDrawArrays", mode, count, 0, -1, NULL);
    cyan_predraw_capture(count);
    maybe_force_depth_clear(count);
    int dm_lap = 0;
    dm_lap_begin(&dm_lap);
    rgl_glDrawArrays(mode, first, count);
    dm_lap_end(dm_lap);
    if (dm_lap) {
        static unsigned long n = 0;
        n++;
        if (n <= 120 || n % 300 == 0)
            dlog_msg("depthmask-lap glDrawArrays count=%d (kald=%lu)",
                     (int)count, n);
    }
    cyan_postdraw_sample("drawArrays", 0, count);
    if (getenv("CYAN_SHADOW_PROBE"))
        cyan_shadow_probe("drawArrays", mode, count, 0, NULL, first, -1);
}

static void hook_glDrawElementsInstanced(GLenum mode, GLsizei count,
                                         GLenum type, const void *indices,
                                         GLsizei primcount)
{
    rgl_resolve_all();
    dump_draw("glDrawElementsInstanced", mode, count, type, primcount,
              indices);
    cyan_predraw_capture(count);
    maybe_force_depth_clear(count);
    int dm_lap = 0;
    dm_lap_begin(&dm_lap);
    if (rgl_glDrawElementsInstanced)
        rgl_glDrawElementsInstanced(mode, count, type, indices, primcount);
    else
        rgl_glDrawElements(mode, count, type, indices);
    dm_lap_end(dm_lap);
    if (dm_lap) {
        static unsigned long n = 0;
        n++;
        if (n <= 120 || n % 300 == 0)
            dlog_msg("depthmask-lap glDrawElementsInstanced count=%d "
                     "primcount=%d (kald=%lu)",
                     (int)count, (int)primcount, n);
    }
    {
        GLint prog = 0;
        rgl_glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        cyan_postdraw_sample("drawElementsInstanced", (GLuint)prog, count);
    }
    if (getenv("CYAN_SHADOW_PROBE"))
        cyan_shadow_probe("drawElementsInstanced", mode, count, type, indices,
                          0, primcount);
}

static void hook_glDrawArraysInstanced(GLenum mode, GLint first,
                                       GLsizei count, GLsizei primcount)
{
    rgl_resolve_all();
    dump_draw("glDrawArraysInstanced", mode, count, 0, primcount, NULL);
    cyan_predraw_capture(count);
    maybe_force_depth_clear(count);
    int dm_lap = 0;
    dm_lap_begin(&dm_lap);
    if (rgl_glDrawArraysInstanced)
        rgl_glDrawArraysInstanced(mode, first, count, primcount);
    else
        rgl_glDrawArrays(mode, first, count);
    dm_lap_end(dm_lap);
    if (dm_lap) {
        static unsigned long n = 0;
        n++;
        if (n <= 120 || n % 300 == 0)
            dlog_msg("depthmask-lap glDrawArraysInstanced count=%d "
                     "primcount=%d (kald=%lu)",
                     (int)count, (int)primcount, n);
    }
    {
        GLint prog = 0;
        rgl_glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        cyan_postdraw_sample("drawArraysInstanced", (GLuint)prog, count);
    }
    if (getenv("CYAN_SHADOW_PROBE"))
        cyan_shadow_probe("drawArraysInstanced", mode, count, 0, NULL, first,
                          primcount);
}

static void hook_glUseProgram(GLuint prog)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 40 || n % 200 == 0)
        dlog_msg("useProgram prog=%u (kald=%lu)", (unsigned)prog, n);
    rgl_glUseProgram(prog);
}

static void hook_glLinkProgram(GLuint prog)
{
    rgl_resolve_all();
    rgl_glLinkProgram(prog);
    GLint ok = 0, nu = 0, na = 0;
    rgl_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    rgl_glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &nu);
    rgl_glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &na);
    dlog_msg("linkProgram prog=%u ok=%d active-uniforms=%d active-attribs=%d",
             (unsigned)prog, (int)ok, (int)nu, (int)na);
}

static void hook_glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 40 || n % 400 == 0)
        dlog_msg("viewport x=%d y=%d w=%d h=%d (kald=%lu)", (int)x, (int)y,
                 (int)w, (int)h, n);
    rgl_glViewport(x, y, w, h);
}

static void hook_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 20 || n % 300 == 0)
        dlog_msg("clearColor r=%.4f g=%.4f b=%.4f a=%.4f (kald=%lu)",
                 (double)r, (double)g, (double)b, (double)a, n);
    rgl_glClearColor(r, g, b, a);
}

static void hook_glClearDepthf(GLfloat d)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 60 || n % 200 == 0)
        dlog_msg("clearDepthf d=%g (kald=%lu)", (double)d, n);
    {
        static void (*real)(GLfloat);
        if (!real)
            real = (void (*)(GLfloat))real_eglGetProcAddress_fn(
                "glClearDepthf");
        if (!real)
            real = (void (*)(GLfloat))real_eglGetProcAddress_fn(
                "glClearDepth");
        if (real)
            real(d);
    }
}

static void hook_glClear(GLbitfield mask)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 30 || n % 200 == 0) {
        GLint fbo = -1, vp[4] = {0, 0, 0, 0};
        if (rgl_glGetIntegerv) {
            rgl_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            rgl_glGetIntegerv(GL_VIEWPORT, vp);
        }
        dlog_msg("clear mask=0x%x fbo=%d viewport=[%d,%d %dx%d] (kald=%lu)",
                 (unsigned)mask, (int)fbo, vp[0], vp[1], vp[2], vp[3], n);
    }
    rgl_glClear(mask);
}

/* ---- FBO/tekstur-sporing: hvad er fbo=3, og hvilken størrelse har dens
 *      attachment? (cyan-scene: alle scene-draws rammer fbo=3) ---- */
static void hook_glBindFramebuffer(GLenum target, GLuint fb)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 120 || n % 300 == 0)
        dlog_msg("bindFramebuffer target=0x%x fbo=%u (kald=%lu)",
                 (unsigned)target, (unsigned)fb, n);
    rgl_glBindFramebuffer(target, fb);
}

static void hook_glFramebufferTexture2D(GLenum target, GLenum attachment,
                                        GLenum textarget, GLuint texture,
                                        GLint level)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 200 || n % 400 == 0)
        dlog_msg("fboTex target=0x%x attachment=0x%x textarget=0x%x tex=%u "
                 "level=%d (kald=%lu)",
                 (unsigned)target, (unsigned)attachment, (unsigned)textarget,
                 (unsigned)texture, (int)level, n);
    rgl_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

static void hook_glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                           GLenum rbtarget, GLuint rb)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 120 || n % 400 == 0)
        dlog_msg("fboRb target=0x%x attachment=0x%x rbtarget=0x%x rb=%u "
                 "(kald=%lu)",
                 (unsigned)target, (unsigned)attachment, (unsigned)rbtarget,
                 (unsigned)rb, n);
    rgl_glFramebufferRenderbuffer(target, attachment, rbtarget, rb);
}

static void hook_glTexImage2D(GLenum target, GLint level, GLint ifmt,
                              GLsizei w, GLsizei h, GLint border, GLenum fmt,
                              GLenum type, const void *pixels)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 250 || n % 500 == 0) {
        GLint tex = 0;
        rgl_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        dlog_msg("texImage2D tex=%u level=%d %dx%d ifmt=0x%x fmt=0x%x "
                 "type=0x%x pix=%p (kald=%lu)",
                 (unsigned)tex, (int)level, (int)w, (int)h, (unsigned)ifmt,
                 (unsigned)fmt, (unsigned)type, pixels, n);
    }
    rgl_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, pixels);
}

static void hook_glTexStorage2D(GLenum target, GLsizei levels, GLenum ifmt,
                                GLsizei w, GLsizei h)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n <= 250 || n % 500 == 0) {
        GLint tex = 0;
        rgl_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        dlog_msg("texStorage2D tex=%u levels=%d %dx%d ifmt=0x%x (kald=%lu)",
                 (unsigned)tex, (int)levels, (int)w, (int)h,
                 (unsigned)ifmt, n);
    }
    rgl_glTexStorage2D(target, levels, ifmt, w, h);
}

static void hook_glDrawBuffers(GLsizei n, const GLenum *bufs)
{
    static unsigned long c = 0;
    rgl_resolve_all();
    c++;
    if (c <= 120 || c % 250 == 0) {
        char line[512];
        int o = snprintf(line, sizeof line, "drawBuffers n=%d (kald=%lu) [",
                         (int)n, c);
        for (GLsizei i = 0; i < n && i < 8; i++) {
            if (o >= 0 && o < (int)sizeof line)
                o += snprintf(line + o, sizeof line - (size_t)o, "%s0x%x",
                              i ? "," : "", (unsigned)bufs[i]);
        }
        if (o >= 0 && o < (int)sizeof line)
            snprintf(line + o, sizeof line - (size_t)o, "]");
        dlog_line(line);
    }
    if (rgl_glDrawBuffers)
        rgl_glDrawBuffers(n, bufs);
}

static void hook_glVertexAttribDivisor(GLuint index, GLuint divisor)
{
    static unsigned long c = 0;
    rgl_resolve_all();
    c++;
    if (c <= 120 || c % 100 == 0) {
        GLint prog = 0;
        rgl_glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        dlog_msg("vertexAttribDivisor index=%u divisor=%u prog=%d "
                 "(kald=%lu)", (unsigned)index, (unsigned)divisor,
                 (int)prog, c);
    }
    if (rgl_glVertexAttribDivisor)
        rgl_glVertexAttribDivisor(index, divisor);
}

/* ---- swap-readback (cyan-scene): prøvetag det aktuelle read-framebuffer
 *      lige FØR present. preserveDrawingBuffer=false gør ekstern readPixels
 *      ubrugelig (bufferet ryddes efter present — målt: ensartet sort/cyan);
 *      her læser vi indholdet mens scenen stadig står i FBO'en. ---- */
static void cyan_swap_sample(void)
{
    static unsigned long n = 0;
    rgl_resolve_all();
    n++;
    if (n > 80 && n % 25 != 0)
        return;
    if (!rgl_glReadPixels || !rgl_glGetIntegerv)
        return;
    GLint rfbo = 0, vp[4] = {0, 0, 0, 0};
    rgl_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfbo);
    rgl_glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w < 64 || h < 64 || w > 4096 || h > 4096)
        return;
    unsigned char *buf = malloc((size_t)w * h * 4);
    if (!buf)
        return;
    memset(buf, 0, (size_t)w * h * 4);
    rgl_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    int ng = 9, nf = 17;
    char line[8192];
    int o = snprintf(line, sizeof line,
                     "swapSample rfbo=%d %dx%d (swap-kald=%lu) grid:",
                     (int)rfbo, w, h, n);
    int uni[32] = {0};
    int nuniq = 0;
    for (int j = 0; j < ng; j++) {
        int row = (j * (h - 1)) / (ng - 1);
        for (int i = 0; i < nf; i++) {
            int col = (i * (w - 1)) / (nf - 1);
            const unsigned char *q = buf + ((size_t)row * w + col) * 4;
            int key = (q[0] >> 5) << 6 | (q[1] >> 5) << 3 | (q[2] >> 5);
            int found = -1;
            for (int u = 0; u < nuniq; u++) {
                if (uni[u * 2] == key) {
                    found = u;
                    break;
                }
            }
            if (found < 0 && nuniq < 32) {
                found = nuniq;
                uni[found * 2] = key;
                uni[found * 2 + 1] = 0;
                nuniq++;
            }
            if (found >= 0)
                uni[found * 2 + 1]++;
            if (o >= 0 && o < (int)sizeof line)
                o += snprintf(line + o, sizeof line - (size_t)o, "%s%d,%d,%d",
                              i ? " " : "", q[0], q[1], q[2]);
        }
    }
    if (o >= 0 && o < (int)sizeof line)
        snprintf(line + o, sizeof line - (size_t)o, " | unikke-kvantiserede=%d",
                 nuniq);
    dlog_line(line);
    free(buf);
}

/* ---- før-draw-prøve (cyan-scene): prøvetag read-framebufferen LIGE FØR et
 *      stort verdens-draw, så efter-draw-prøven kan vise OM draws skriver
 *      pixels (pre/post-diff). ---- */
static unsigned char pre_grid[3][13][4];
static int have_pre_grid = 0;

static void cyan_predraw_capture(GLsizei count)
{
    static unsigned long big = 0;
    if (count < 512)
        return;
    big++;
    if (big > 80 && big % 250 != 0)
        return;
    if (!rgl_glReadPixels || !rgl_glGetIntegerv)
        return;
    GLint rfbo = 0, vp[4] = {0, 0, 0, 0};
    rgl_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfbo);
    rgl_glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w < 64 || h < 64 || w > 4096 || h > 4096)
        return;
    unsigned char *buf = malloc((size_t)w * h * 4);
    if (!buf)
        return;
    memset(buf, 0, (size_t)w * h * 4);
    rgl_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    have_pre_grid = 1;
    for (int j = 0; j < 3; j++) {
        int row = (int)((double)h * (0.25 + 0.25 * j));
        for (int i = 0; i < 13; i++) {
            int col = (i * (w - 1)) / 12;
            const unsigned char *q = buf + ((size_t)row * w + col) * 4;
            pre_grid[j][i][0] = q[0];
            pre_grid[j][i][1] = q[1];
            pre_grid[j][i][2] = q[2];
            pre_grid[j][i][3] = q[3];
        }
    }
    free(buf);
}

/* ---- SHADOW-DRAW-PROBE (8. sep 2026 aften, vnext6): lige efter det rigtige
 *      store draw køres ET EKSTRA draw med samme tilstand — ikke-instanced
 *      (glDrawElements/glDrawArrays) — og read-framebufferen aflæses. Svarer
 *      på om live-kontekstens draw-kald selv er brudt (offline-replay
 *      rasteriserer samme data+shaders). Skriver skyggedrawet IKKE pixels,
 *      køres varianter med DEPTH_TEST fra (og CULL_FACE fra hvis depth ikke
 *      er porten). Hvis skyggedrawet skriver pixels, gentages også det
 *      rigtige instanced-kald for at se om instancing-stien live er
 *      forskellig. Basen stash'es af postdraw-prøven. ---- */
static unsigned char *shadow_base_buf = NULL;
static int shadow_base_w = 0;
static int shadow_base_h = 0;
static int shadow_base_ready = 0;

static int shadow_cadence(unsigned long big)
{
    return big <= 40 || (big > 80 && big % 250 == 0);
}

static void count_nonsky(const unsigned char *buf, int w, int h,
                         unsigned long *ns, int *x0, int *y0, int *x1,
                         int *y1)
{
    *ns = 0;
    *x0 = w;
    *y0 = h;
    *x1 = -1;
    *y1 = -1;
    for (int j = 0; j < h; j++) {
        const unsigned char *row = buf + (size_t)j * w * 4;
        for (int i = 0; i < w; i++) {
            const unsigned char *q = row + (size_t)i * 4;
            int dr = q[0] - 130, dg = q[1] - 255, db = q[2] - 238;
            if (dr < 0) dr = -dr;
            if (dg < 0) dg = -dg;
            if (db < 0) db = -db;
            if (dr > 40 || dg > 30 || db > 40) {
                (*ns)++;
                if (i < *x0) *x0 = i;
                if (i > *x1) *x1 = i;
                if (j < *y0) *y0 = j;
                if (j > *y1) *y1 = j;
            }
        }
    }
}

static unsigned long buf_diff_count(const unsigned char *a,
                                    const unsigned char *b, int w, int h,
                                    int *x0, int *y0, int *x1, int *y1,
                                    unsigned char first[4])
{
    unsigned long n = 0;
    *x0 = w;
    *y0 = h;
    *x1 = -1;
    *y1 = -1;
    first[0] = first[1] = first[2] = first[3] = 0;
    size_t stride = (size_t)w * 4;
    for (int j = 0; j < h && n < 2000000; j++) {
        const unsigned char *ra = a + (size_t)j * stride;
        const unsigned char *rb = b + (size_t)j * stride;
        for (int i = 0; i < w && n < 2000000; i++) {
            const unsigned char *qa = ra + (size_t)i * 4;
            const unsigned char *qb = rb + (size_t)i * 4;
            if (qa[0] != qb[0] || qa[1] != qb[1] || qa[2] != qb[2] ||
                qa[3] != qb[3]) {
                if (!n) {
                    first[0] = qb[0];
                    first[1] = qb[1];
                    first[2] = qb[2];
                    first[3] = qb[3];
                }
                n++;
                if (i < *x0) *x0 = i;
                if (i > *x1) *x1 = i;
                if (j < *y0) *y0 = j;
                if (j > *y1) *y1 = j;
            }
        }
    }
    return n;
}

static int shadow_read_fb(unsigned char **out, int *w, int *h)
{
    if (!rgl_glReadPixels || !rgl_glGetIntegerv)
        return 0;
    GLint vp[4] = {0, 0, 0, 0};
    rgl_glGetIntegerv(GL_VIEWPORT, vp);
    *w = vp[2];
    *h = vp[3];
    if (*w < 64 || *h < 64 || *w > 4096 || *h > 4096)
        return 0;
    unsigned char *buf = malloc((size_t)*w * *h * 4);
    if (!buf)
        return 0;
    memset(buf, 0, (size_t)*w * *h * 4);
    rgl_glReadPixels(0, 0, *w, *h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    *out = buf;
    return 1;
}

static GLenum (*shadow_raw_err_fn)(void);

static GLenum shadow_raw_err(void)
{
    if (!shadow_raw_err_fn)
        shadow_raw_err_fn =
            (GLenum (*)(void))real_eglGetProcAddress_fn("glGetError");
    return shadow_raw_err_fn ? shadow_raw_err_fn() : 0;
}

/* Kører ét variant-skyggedraw med depth/cull midlertidigt slået fra og
 * sammenligner med basen (A = indhold efter det rigtige draw). Gendanner
 * tilstanden bagefter. */
static void shadow_run_variant(const char *label, int depth_off, int cull_off,
                               const char *fn, GLenum mode, GLsizei count,
                               GLenum type, const void *indices, GLint first,
                               char *line, size_t linesz, int *o,
                               unsigned long *vchanged)
{
    unsigned long changed = 0;
    if (!rgl_glEnable || !rgl_glDisable || !rgl_glIsEnabled) {
        if (o && *o >= 0 && *o < (int)linesz)
            *o += snprintf(line + *o, linesz - (size_t)*o,
                           " | var-%s utilgaengelig", label);
        if (vchanged)
            *vchanged = changed;
        return;
    }
    int depth_was = rgl_glIsEnabled(GL_DEPTH_TEST);
    int cull_was = rgl_glIsEnabled(GL_CULL_FACE);
    if (depth_off && depth_was)
        rgl_glDisable(GL_DEPTH_TEST);
    if (cull_off && cull_was)
        rgl_glDisable(GL_CULL_FACE);
    int lap = 0;
    dm_lap_begin(&lap);
    if (strstr(fn, "Arrays"))
        rgl_glDrawArrays(mode, first, count);
    else
        rgl_glDrawElements(mode, count, type, indices);
    dm_lap_end(lap);
    GLenum err = shadow_raw_err();
    if (depth_off && depth_was)
        rgl_glEnable(GL_DEPTH_TEST);
    if (cull_off && cull_was)
        rgl_glEnable(GL_CULL_FACE);
    unsigned char *V = NULL;
    int w = 0, h = 0;
    if (!shadow_read_fb(&V, &w, &h) || w != shadow_base_w ||
        h != shadow_base_h) {
        if (o && *o >= 0 && *o < (int)linesz)
            *o += snprintf(line + *o, linesz - (size_t)*o,
                           " | var-%s aflaesning fejlede", label);
        free(V);
        if (vchanged)
            *vchanged = 0;
        return;
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    unsigned char firstcol[4] = {0, 0, 0, 0};
    changed = buf_diff_count(shadow_base_buf, V, w, h, &x0, &y0, &x1, &y1,
                             firstcol);
    unsigned long ns = 0;
    int nx0 = 0, ny0 = 0, nx1 = 0, ny1 = 0;
    count_nonsky(V, w, h, &ns, &nx0, &ny0, &nx1, &ny1);
    if (o && *o >= 0 && *o < (int)linesz)
        *o += snprintf(line + *o, linesz - (size_t)*o,
                       " | var-%s changed=%lu bbox=x[%d..%d],y[%d..%d] "
                       "nonsky=%lu forste=%d,%d,%d,%d err=0x%x",
                       label, changed, x0, x1, y0, y1, ns, firstcol[0],
                       firstcol[1], firstcol[2], firstcol[3], (unsigned)err);
    free(V);
    if (vchanged)
        *vchanged = changed;
}

static void cyan_shadow_probe(const char *fn, GLenum mode, GLsizei count,
                              GLenum type, const void *indices, GLint first,
                              GLsizei primcount)
{
    static unsigned long big = 0;
    static unsigned long nprobe = 0;
    if (count < 512)
        return;
    big++;
    if (!shadow_cadence(big))
        return;
    if (nprobe >= 400)
        return;
    if (!shadow_base_ready || !shadow_base_buf)
        return;
    if (!rgl_glDrawElements || !rgl_glDrawArrays || !rgl_glReadPixels)
        return;
    nprobe++;
    GLint prog = 0;
    rgl_glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    GLenum e0 = shadow_raw_err();
    int instanced = strstr(fn, "Instanced") != NULL;
    /* 1) skyggedraw: samme tilstand, men IKKE-instanced. */
    {
        int lap = 0;
        dm_lap_begin(&lap);
        if (strstr(fn, "Arrays"))
            rgl_glDrawArrays(mode, first, count);
        else
            rgl_glDrawElements(mode, count, type, indices);
        dm_lap_end(lap);
    }
    GLenum e1 = shadow_raw_err();
    unsigned char *B = NULL;
    int w = 0, h = 0;
    if (!shadow_read_fb(&B, &w, &h)) {
        dlog_msg("SHADOW #%lu fn=%s prog=%u count=%d primcount=%d big=%lu "
                 "aflaesning fejlede",
                 nprobe, fn, (unsigned)prog, (int)count, (int)primcount, big);
        return;
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    unsigned char firstcol[4] = {0, 0, 0, 0};
    unsigned long nsA = 0, nsB = 0;
    int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    count_nonsky(shadow_base_buf, shadow_base_w, shadow_base_h, &nsA,
                 &ax0, &ay0, &ax1, &ay1);
    count_nonsky(B, w, h, &nsB, &bx0, &by0, &bx1, &by1);
    unsigned long changed =
        buf_diff_count(shadow_base_buf, B, w, h, &x0, &y0, &x1, &y1, firstcol);
    char line[2048];
    int o = snprintf(line, sizeof line,
                     "SHADOW #%lu fn=%s prog=%u count=%d primcount=%d mode="
                     "0x%x %dx%d big=%lu | base nonsky=%lu bbox=x[%d..%d],"
                     "y[%d..%d] -> noninst-shadow nonsky=%lu bbox=x[%d..%d],"
                     "y[%d..%d] changed=%lu bbox=x[%d..%d],y[%d..%d] "
                     "forste=%d,%d,%d,%d err0=0x%x err1=0x%x",
                     nprobe, fn, (unsigned)prog, (int)count, (int)primcount,
                     (unsigned)mode, w, h, big, nsA, ax0, ax1, ay0, ay1, nsB,
                     bx0, bx1, by0, by1, changed, x0, x1, y0, y1, firstcol[0],
                     firstcol[1], firstcol[2], firstcol[3], (unsigned)e0,
                     (unsigned)e1);
    if (instanced && changed == 0) {
        unsigned long vd = 0;
        shadow_run_variant("depthoff", 1, 0, fn, mode, count, type, indices,
                           first, line, sizeof line, &o, &vd);
        if (vd == 0)
            shadow_run_variant("culloff", 0, 1, fn, mode, count, type,
                               indices, first, line, sizeof line, &o, NULL);
    }
    if (instanced && changed > 0 && w == shadow_base_w && h == shadow_base_h) {
        /* 2) gentag det RIGTIGE instanced-kald og sammenlign med basen. */
        {
            int lap = 0;
            dm_lap_begin(&lap);
            if (strstr(fn, "Arrays")) {
                if (rgl_glDrawArraysInstanced)
                    rgl_glDrawArraysInstanced(mode, first, count, primcount);
            } else if (rgl_glDrawElementsInstanced) {
                rgl_glDrawElementsInstanced(mode, count, type, indices,
                                            primcount);
            }
            dm_lap_end(lap);
        }
        GLenum e2 = shadow_raw_err();
        unsigned char *C = NULL;
        int w2 = 0, h2 = 0;
        if (shadow_read_fb(&C, &w2, &h2) && w2 == shadow_base_w &&
            h2 == shadow_base_h) {
            unsigned long nsC = 0;
            int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
            count_nonsky(C, w2, h2, &nsC, &cx0, &cy0, &cx1, &cy1);
            int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
            unsigned char rcol[4] = {0, 0, 0, 0};
            unsigned long rechanged =
                buf_diff_count(shadow_base_buf, C, w2, h2, &rx0, &ry0, &rx1,
                               &ry1, rcol);
            if (o >= 0 && o < (int)sizeof line)
                o += snprintf(line + o, sizeof line - (size_t)o,
                              " | inst-shadow nonsky=%lu bbox=x[%d..%d],"
                              "y[%d..%d] changed-vs-base=%lu bbox=x[%d..%d],"
                              "y[%d..%d] forste=%d,%d,%d,%d err2=0x%x",
                              nsC, cx0, cx1, cy0, cy1, rechanged, rx0, rx1,
                              ry0, ry1, rcol[0], rcol[1], rcol[2], rcol[3],
                              (unsigned)e2);
        }
        free(C);
    }
    dlog_line(line);
    free(B);
}

/* ---- efter-draw-prøve (cyan-scene): lige efter et stort verdens-draw læses
 *      read-framebufferen — skrev meshet pixels eller ej? ---- */
static void cyan_postdraw_sample(const char *tag, GLuint prog, GLsizei count)
{
    static unsigned long big = 0;
    if (count < 512)
        return;
    big++;
    shadow_base_ready = 0;
    if (big > 80 && big % 250 != 0)
        return;
    if (!rgl_glReadPixels || !rgl_glGetIntegerv)
        return;
    GLint rfbo = 0, vp[4] = {0, 0, 0, 0};
    rgl_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfbo);
    rgl_glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w < 64 || h < 64 || w > 4096 || h > 4096)
        return;
    unsigned char *buf = malloc((size_t)w * h * 4);
    if (!buf)
        return;
    memset(buf, 0, (size_t)w * h * 4);
    rgl_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    int nf = 13, ng = 3;
    char line[4096];
    int o = snprintf(line, sizeof line,
                     "postdraw %s prog=%u count=%d rfbo=%d %dx%d "
                     "(big-kald=%lu) rækker:",
                     tag, (unsigned)prog, (int)count, (int)rfbo, w, h, big);
    int uni[64] = {0};
    int nuniq = 0;
    for (int j = 0; j < ng; j++) {
        int row = (int)((double)h * (0.25 + 0.25 * j));
        if (j > 0 && o >= 0 && o < (int)sizeof line)
            o += snprintf(line + o, sizeof line - (size_t)o, " |");
        for (int i = 0; i < nf; i++) {
            int col = (i * (w - 1)) / (nf - 1);
            const unsigned char *q = buf + ((size_t)row * w + col) * 4;
            int key = ((q[0] >> 5) << 6) | ((q[1] >> 5) << 3) | (q[2] >> 5);
            int found = -1;
            for (int u2 = 0; u2 < nuniq; u2++) {
                if (uni[u2 * 2] == key) {
                    found = u2;
                    break;
                }
            }
            if (found < 0 && nuniq < 64) {
                found = nuniq;
                uni[found * 2] = key;
                uni[found * 2 + 1] = 0;
                nuniq++;
            }
            if (found >= 0)
                uni[found * 2 + 1]++;
            if (o >= 0 && o < (int)sizeof line)
                o += snprintf(line + o, sizeof line - (size_t)o,
                              "%s%d,%d,%d", i ? " " : "",
                              q[0], q[1], q[2]);
        }
    }
    if (o >= 0 && o < (int)sizeof line)
        snprintf(line + o, sizeof line - (size_t)o, " | unikke=%d", nuniq);
    int diff = -1;
    if (have_pre_grid) {
        diff = 0;
        for (int j = 0; j < 3; j++) {
            int row = (int)((double)h * (0.25 + 0.25 * j));
            for (int i = 0; i < 13; i++) {
                int col = (i * (w - 1)) / 12;
                const unsigned char *q = buf + ((size_t)row * w + col) * 4;
                if (q[0] != pre_grid[j][i][0] ||
                    q[1] != pre_grid[j][i][1] ||
                    q[2] != pre_grid[j][i][2])
                    diff++;
            }
        }
        have_pre_grid = 0;
    }
    o += snprintf(line + o, sizeof line - (size_t)o, " | pre-post-diff=%d",
                  diff);
    {
        unsigned long ns = 0;
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int j = 0; j < h; j++) {
            const unsigned char *row = buf + (size_t)j * w * 4;
            for (int i = 0; i < w; i++) {
                const unsigned char *q = row + (size_t)i * 4;
                int dr = q[0] - 130;
                int dg = q[1] - 255;
                int db = q[2] - 238;
                if (dr < 0) dr = -dr;
                if (dg < 0) dg = -dg;
                if (db < 0) db = -db;
                if (dr > 40 || dg > 30 || db > 40) {
                    ns++;
                    if (i < x0) x0 = i;
                    if (i > x1) x1 = i;
                    if (j < y0) y0 = j;
                    if (j > y1) y1 = j;
                }
            }
        }
        if (ns)
            o += snprintf(line + o, sizeof line - (size_t)o,
                          " | nonsky=%lu bbox=x[%d..%d],y[%d..%d]",
                          ns, x0, x1, y0, y1);
        else
            o += snprintf(line + o, sizeof line - (size_t)o, " | nonsky=0");
    }
    dlog_line(line);
    if (shadow_cadence(big)) {
        unsigned char *cp = malloc((size_t)w * h * 4);
        if (cp) {
            memcpy(cp, buf, (size_t)w * h * 4);
            if (shadow_base_buf)
                free(shadow_base_buf);
            shadow_base_buf = cp;
            shadow_base_w = w;
            shadow_base_h = h;
            shadow_base_ready = 1;
        }
    }
    free(buf);
}

typedef __eglMustCastToProperFunctionPointerType (*real_eglGetProcAddress_ret_t)(const char *);

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    if (!real_eglGetProcAddress_fn)
        real_eglGetProcAddress_fn =
            (__eglMustCastToProperFunctionPointerType (*)(const char *))
            dlsym(RTLD_NEXT, "eglGetProcAddress");
    if (name && !strcmp(name, "glShaderSource"))
        return (__eglMustCastToProperFunctionPointerType)hook_glShaderSource;
    if (name && !strcmp(name, "glCompileShader"))
        return (__eglMustCastToProperFunctionPointerType)hook_glCompileShader;
    if (name && !strcmp(name, "glGetError"))
        return (__eglMustCastToProperFunctionPointerType)hook_glGetError;
    if (name && !strcmp(name, "glDrawElements"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawElements;
    if (name && !strcmp(name, "glDrawArrays"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawArrays;
    if (name && !strcmp(name, "glDrawElementsInstanced"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawElementsInstanced;
    if (name && !strcmp(name, "glDrawElementsInstancedANGLE"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawElementsInstanced;
    if (name && !strcmp(name, "glDrawArraysInstanced"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawArraysInstanced;
    if (name && !strcmp(name, "glDrawArraysInstancedANGLE"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawArraysInstanced;
    if (name && !strcmp(name, "glUseProgram"))
        return (__eglMustCastToProperFunctionPointerType)hook_glUseProgram;
    if (name && !strcmp(name, "glLinkProgram"))
        return (__eglMustCastToProperFunctionPointerType)hook_glLinkProgram;
    if (name && !strcmp(name, "glViewport"))
        return (__eglMustCastToProperFunctionPointerType)hook_glViewport;
    if (name && !strcmp(name, "glClearColor"))
        return (__eglMustCastToProperFunctionPointerType)hook_glClearColor;
    if (name && !strcmp(name, "glClearDepthf"))
        return (__eglMustCastToProperFunctionPointerType)hook_glClearDepthf;
    if (name && !strcmp(name, "glClearDepth"))
        return (__eglMustCastToProperFunctionPointerType)hook_glClearDepthf;
    if (name && !strcmp(name, "glClear"))
        return (__eglMustCastToProperFunctionPointerType)hook_glClear;
    if (name && !strcmp(name, "glBindFramebuffer"))
        return (__eglMustCastToProperFunctionPointerType)hook_glBindFramebuffer;
    if (name && !strcmp(name, "glFramebufferTexture2D"))
        return (__eglMustCastToProperFunctionPointerType)hook_glFramebufferTexture2D;
    if (name && !strcmp(name, "glFramebufferRenderbuffer"))
        return (__eglMustCastToProperFunctionPointerType)hook_glFramebufferRenderbuffer;
    if (name && !strcmp(name, "glTexImage2D"))
        return (__eglMustCastToProperFunctionPointerType)hook_glTexImage2D;
    if (name && !strcmp(name, "glTexStorage2D"))
        return (__eglMustCastToProperFunctionPointerType)hook_glTexStorage2D;
    if (name && !strcmp(name, "glDrawBuffers"))
        return (__eglMustCastToProperFunctionPointerType)hook_glDrawBuffers;
    if (name && !strcmp(name, "glVertexAttribDivisor"))
        return (__eglMustCastToProperFunctionPointerType)hook_glVertexAttribDivisor;
    if (name && !strcmp(name, "glVertexAttribDivisorANGLE"))
        return (__eglMustCastToProperFunctionPointerType)hook_glVertexAttribDivisor;
    return ((real_eglGetProcAddress_ret_t)real_eglGetProcAddress_fn)(name);
}
