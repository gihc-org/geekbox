/* glesv2_proxy.c — tynd proxy for /opt/hybris/libGLESv2.so.2.0.0.
 *
 * Huker glShaderSource og omskriver shader-kilden FØR den sendes til driveren:
 *   - fjerner "#extension GL_EXT_frag_depth ..." direktivlinjer
 *   - erstatter "gl_FragDepthEXT" med "gl_FragDepth" (ES3-kernen kan skrive
 *     gl_FragDepth; spillet kører WebGL2/ES3 — målt 26. aug 2026)
 * NB (målt 26. aug): frigør IKKE de omskrevne strenge efter glShaderSource —
 * driveren kan beholde pointerne til glCompileShader (hard lockup 26. aug med
 * free). Læk pr. shader accepteres i forsøget.
 * Log: /tmp/fragdepth_probe.log.
 *
 * Byg på boksen:
 *   gcc -shared -fPIC -Wl,-soname,libGLESv2.so.2 -o /opt/hybris/libGLESv2.so.2.0.0 \
 *       glesv2_proxy.c -L/opt/hybris -l:libGLESv2_r.so -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>

static FILE *plog(void)
{
    return fopen("/tmp/fragdepth_probe.log", "a");
}

typedef void (*real_glShaderSource_t)(GLuint, GLsizei, const GLchar **,
                                      const GLint *);

void glShaderSource(GLuint shader, GLsizei count, const GLchar **str,
                    const GLint *len)
{
    static real_glShaderSource_t real;
    if (!real) real = (real_glShaderSource_t)dlsym(RTLD_NEXT, "glShaderSource");

    FILE *f = plog();
    for (GLsizei i = 0; i < count && str && str[i]; i++) {
        fprintf(f, "=== shader %u[%d] SRC: %.3000s\n", shader, i, str[i]);
    }
    fflush(f);

    int changed = 0;
    for (GLsizei i = 0; i < count && str && str[i]; i++) {
        if (strstr(str[i], "GL_EXT_frag_depth") || strstr(str[i], "gl_FragDepthEXT")) {
            changed = 1;
            break;
        }
    }
    if (!changed) {
        if (f) fclose(f);
        real(shader, count, str, len);
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
        newstr[i] = out;
        newlen[i] = o;
        if (f) fprintf(f, "--- shader %u[%d] REWRITTEN: %s\n", shader, i, out);
    }
    if (f) fclose(f);
    real(shader, count, (const GLchar **)newstr, newlen);
    /* Læk bevidst: driveren kan holde pointerne til glCompileShader.
     * (newstr/newlen beholdes også — minimalt læk pr. shader.) */
}

typedef void (*real_glCompileShader_t)(GLuint);

void glCompileShader(GLuint shader)
{
    static real_glCompileShader_t real_compile;
    static void (*real_getinfo)(GLuint, GLenum, GLint *);
    static void (*real_getlog)(GLuint, GLsizei, GLsizei *, GLchar *);
    if (!real_compile) {
        real_compile = (real_glCompileShader_t)dlsym(RTLD_NEXT, "glCompileShader");
        real_getinfo = (void (*)(GLuint, GLenum, GLint *))dlsym(RTLD_NEXT, "glGetShaderiv");
        real_getlog = (void (*)(GLuint, GLsizei, GLsizei *, GLchar *))dlsym(RTLD_NEXT, "glGetShaderInfoLog");
    }
    real_compile(shader);
    GLint ok = 0;
    real_getinfo(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        real_getinfo(shader, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            GLchar *buf = malloc(len);
            GLsizei n = 0;
            real_getlog(shader, len, &n, buf);
            FILE *f = plog();
            if (f) {
                fprintf(f, "!!! shader %u COMPILE_FAIL: %s\n", shader, buf);
                fclose(f);
            }
            free(buf);
        }
    }
}
