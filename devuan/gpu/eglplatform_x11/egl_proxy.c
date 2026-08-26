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
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static FILE *plog(void)
{
    return fopen("/tmp/fragdepth_probe.log", "a");
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
    FILE *f = plog();
    if (f) {
        fprintf(f, "eglCreateContext version=%d\n", ver);
        fclose(f);
    }
    return real(dpy, cfg, share, attr);
}

/* ---- shader-omskrivning (glShaderSource via eglGetProcAddress) ---- */

static void (*real_glShaderSource)(GLuint, GLsizei, const GLchar **, const GLint *);
static void (*real_glCompileShader)(GLuint);
static void (*real_glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*real_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static __eglMustCastToProperFunctionPointerType (*real_eglGetProcAddress_fn)(const char *);

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
        fprintf(f, "=== shader %u[%d] SRC: %.20000s\n", shader, i, str[i]);
        char path[128];
        snprintf(path, sizeof path, "/tmp/shaders/%u_%d.glsl", shader, i);
        FILE *sf = fopen(path, "w");
        if (sf) {
            int sl = (len && len[i] >= 0) ? len[i] : (int)strlen(str[i]);
            fwrite(str[i], 1, sl, sf);
            fclose(sf);
        }
    }
    fflush(f);

    int changed = 0;
    for (GLsizei i = 0; i < count && str && str[i]; i++) {
        if (strstr(str[i], "GL_EXT_frag_depth") || strstr(str[i], "gl_FragDepthEXT") ||
            strstr(str[i], "vSupport")) {
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
                strstr(s + j, "frag_depth")) {
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
    return ((real_eglGetProcAddress_ret_t)real_eglGetProcAddress_fn)(name);
}
