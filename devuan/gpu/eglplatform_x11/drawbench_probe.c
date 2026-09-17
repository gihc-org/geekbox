/* drawbench_probe.c - hvad koster et draw-kald paa 1.5-stakken?
 *
 * Baggrund (17. sep 2026): Subway Surfers' frames bestaar af ~1.500-2.800
 * draw-kald (maalt i proxy-loggen: 4.000-7.500 draws/s ved 2,7 fps). Alle er
 * glDrawElementsInstanced med primcount=1. Hvis et draw-kald koster ~0,2 ms i
 * driveren, giver 2.000 kald ~400 ms pr. frame = ~2,5 fps — praecis det vi
 * maaler. Denne probe maaler prisen pr. kald for de tre veje, saa vi kan se om
 * der er noget at hente (fx at kalde glDrawElements naar primcount=1).
 *
 * Byg/koer paa boksen:
 *   gcc -O2 -o /root/drawbench_probe /root/drawbench_probe.c \
 *       -I/usr/local/include -L/opt/hybris -Wl,--no-as-needed \
 *       -l:libEGL_r.so -l:libGLESv2.so.2 -lhybris-common \
 *       -landroid-properties -ldl -lrt -lm
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *       EGL_PLATFORM=x11 DISPLAY=:0 /root/drawbench_probe [antal]
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (*PFN_DrawElementsInstanced)(GLenum, GLsizei, GLenum,
                                          const void *, GLsizei);

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static const char *VS =
    "attribute vec3 aPos;\n"
    "void main() { gl_Position = vec4(aPos, 1.0); }\n";
static const char *FS =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n";

static GLuint make_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
        fprintf(stderr, "shader-fejl: %s\n", log);
        exit(1);
    }
    return s;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, NULL, NULL);
    EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint nc = 0;
    eglChooseConfig(d, a, &cfg, 1, &nc);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (c == EGL_NO_CONTEXT) {
        fprintf(stderr, "EGL ES3-kontekst fejlede\n");
        return 1;
    }
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
    PFN_DrawElementsInstanced inst =
        (PFN_DrawElementsInstanced)eglGetProcAddress(
            "glDrawElementsInstanced");
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("glDrawElementsInstanced: %p\n\n", (void *)inst);

    const int W = 836, H = 470;
    GLuint fbo, tex, rb;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, rb);
    glViewport(0, 0, W, H);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, make_shader(GL_VERTEX_SHADER, VS));
    glAttachShader(prog, make_shader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(prog);
    glUseProgram(prog);
    GLint attr = glGetAttribLocation(prog, "aPos");
    /* lille mesh: 4 verts, 6 indices - som spillets smaabeskaeringer */
    GLfloat v[4 * 3] = { -1, -1, 0.5f, 1, -1, 0.5f, -1, 1, 0.5f, 1, 1, 0.5f };
    unsigned short idx[6] = { 0, 1, 2, 0, 2, 3 };
    GLuint vbo, ebo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)attr);
    glVertexAttribPointer((GLuint)attr, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    printf("== %d draw-kald af gangen ==\n", n);
    /* 1) almindelig glDrawElements */
    glClear(GL_COLOR_BUFFER_BIT);
    double t0 = now_ms();
    for (int i = 0; i < n; i++) {
        glViewport(0, 0, W, H);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
    }
    glFinish();
    double dt = now_ms() - t0;
    printf("glDrawElements            : %8.1f ms for %d kald = %.3f ms/kald "
           "(%.0f kald/s)\n", dt, n, dt / n, n / (dt / 1000.0));

    /* 2) glDrawElementsInstanced med primcount=1 (spillets vej) */
    glClear(GL_COLOR_BUFFER_BIT);
    t0 = now_ms();
    for (int i = 0; i < n; i++) {
        glViewport(0, 0, W, H);
        inst(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 1);
    }
    glFinish();
    dt = now_ms() - t0;
    printf("glDrawElementsInstanced=1 : %8.1f ms for %d kald = %.3f ms/kald "
           "(%.0f kald/s)\n", dt, n, dt / n, n / (dt / 1000.0));

    /* 3) som 2, men uden at gentage viewport-kaldet (spillet kalder det ikke
     *    hvert draw) */
    glClear(GL_COLOR_BUFFER_BIT);
    t0 = now_ms();
    for (int i = 0; i < n; i++)
        inst(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 1);
    glFinish();
    dt = now_ms() - t0;
    printf("instanced (uden viewport) : %8.1f ms for %d kald = %.3f ms/kald "
           "(%.0f kald/s)\n", dt, n, dt / n, n / (dt / 1000.0));

    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
        printf("gl-fejl 0x%x\n", (unsigned)e);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, c);
    eglTerminate(d);
    return 0;
}
