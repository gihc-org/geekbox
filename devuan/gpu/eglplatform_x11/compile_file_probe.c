/* compile_file_probe.c — kompilerer en shader-fil med driveren (til bisektion
 * af WebRender cs_blur-compile-fejlen). Brug:
 *   compile_file_probe <fil.glsl> vertex|fragment [es3|es2]
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) { printf("brug: %s <fil> vertex|fragment [es3|es2]\n", argv[0]); return 2; }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { printf("kan ikke åbne %s\n", argv[1]); return 2; }
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    fseek(in, 0, SEEK_SET);
    char *src = malloc(sz + 1);
    fread(src, 1, sz, in);
    src[sz] = 0;
    fclose(in);

    int es3 = !(argc > 3 && !strcmp(argv[3], "es2"));
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, NULL, NULL)) { printf("init fail\n"); return 1; }
    EGLint a[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n;
    if (!eglChooseConfig(d, a, &cfg, 1, &n) || n < 1) { printf("no cfg\n"); return 1; }
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, es3 ? 3 : 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
        printf("ctx fail\n"); return 1;
    }
    GLuint s = glCreateShader(!strcmp(argv[2], "vertex") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const char *p = src;
    glShaderSource(s, 1, &p, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[4096] = { 0 };
    glGetShaderInfoLog(s, 4095, NULL, log);
    printf("compile=%s info=%s\n", ok ? "OK" : "FEJL", log);
    return ok ? 0 : 1;
}
