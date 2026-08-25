#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>
typedef struct __GLsync *GLsync;
typedef uint64_t GLuint64;
typedef int64_t GLint64;
#include <GLES3/gl3.h>
#include <stdio.h>

static void test_compile(EGLDisplay d, EGLConfig cfg, int es3, const char *tag)
{
    EGLint attr[] = { EGL_CONTEXT_CLIENT_VERSION, es3 ? 3 : 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, attr);
    if (c == EGL_NO_CONTEXT) {
        printf("%s: ctx fail\n", tag);
        return;
    }
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
        printf("%s: mc fail\n", tag);
        return;
    }
    const char *src =
        "#extension GL_EXT_draw_buffers : require\n"
        "void main(){ gl_FragColor=vec4(1.0); }\n";
    GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[512] = { 0 };
    glGetShaderInfoLog(s, 511, NULL, log);
    printf("%s: compile=%s info=%s\n", tag, ok ? "OK" : "FEJL", log);
    glDeleteShader(s);
    eglDestroyContext(d, c);
}

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, NULL, NULL)) {
        printf("init fail\n");
        return 1;
    }
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) {
        printf("no cfg\n");
        return 1;
    }
    test_compile(d, cfg, 0, "ES2");
    test_compile(d, cfg, 1, "ES3");
    return 0;
}
