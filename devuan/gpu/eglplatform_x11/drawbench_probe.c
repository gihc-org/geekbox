/* drawbench_probe.c - hvad koster et draw-kald paa 1.5-stakken?
 *
 * Baggrund (17. sep 2026): Subway Surfers' frames bestaar af ~1.500-2.800
 * draw-kald (maalt i proxy-loggen: 4.000-7.500 draws/s ved 2,7 fps). Alle er
 * glDrawElementsInstanced med primcount=1. Hvis et draw-kald koster ~0,2 ms i
 * driveren, giver 2.000 kald ~400 ms pr. frame = ~2,5 fps — praecis det vi
 * maaler. Denne probe maaler prisen pr. kald for de tre veje, saa vi kan se om
 * der er noget at hente (fx at kalde glDrawElements naar primcount=1).
 *
 * Udvidet 19. sep 2026 (variant 4-8): proben linker DIREKTE til
 * libEGL_r.so (den aegte hybris-EGL) og maaler derfor driveren alene — vores
 * proxy (installeret som /opt/hybris/libEGL.so.1.0.0) rammes kun naar appen
 * henter GL-funktioner via eglGetProcAddress, og det goer Firefox. Proxyens
 * hot path laegger selv GL-forespoergsler oveni HVERT draw:
 *   dm_lap_begin: glIsEnabled(GL_DEPTH_TEST) + (naar depth-test er fra)
 *                 glGetIntegerv(GL_DEPTH_WRITEMASK)
 *   dm_lap:       glDepthMask(GL_FALSE) ... glDepthMask(GL_TRUE)
 *   postdraw:     glGetIntegerv(GL_CURRENT_PROGRAM)
 * Variant 4-8 efterligner netop disse kald, saa vi kan se hvor meget af
 * fps-loftet der er DRIVEREN og hvor meget der er vores egen instrumentering.
 * DEPTH_TEST staar slaaet fra her (som spillets UI-draws), saa grenene i
 * proxyen rammes praecis som i spillet.
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

/* hvilke proxy-kald efterlignes (bit 0-2) */
#define B_DEPTHQ 1 /* glIsEnabled(GL_DEPTH_TEST) + glGetIntegerv(DEPTH_WRITEMASK) */
#define B_PROGQ  2 /* glGetIntegerv(GL_CURRENT_PROGRAM) */
#define B_MASKL  4 /* glDepthMask(GL_FALSE)/glDepthMask(GL_TRUE) omkring drawet */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* En variant af instanced-loekken med proxyens ekstra kald. Returnerer
 * ms/kald. Naar depth-test er slaaet fra (som i spillet) laeser dm_lap_begin
 * DEPTH_WRITEMASK og slaar masken fra/ til — praecis som proxyen. */
static double bench_variant(int n, PFN_DrawElementsInstanced inst, int flags)
{
    glClear(GL_COLOR_BUFFER_BIT);
    double t0 = now_ms();
    for (int i = 0; i < n; i++) {
        int lap = 0;
        if (flags & B_DEPTHQ) {
            if (!glIsEnabled(GL_DEPTH_TEST)) {
                GLint dm = 0;
                glGetIntegerv(GL_DEPTH_WRITEMASK, &dm);
                if (dm) {
                    glDepthMask(GL_FALSE);
                    lap = 1;
                }
            }
        }
        if ((flags & B_MASKL) && !lap) {
            glDepthMask(GL_FALSE);
            lap = 1;
        }
        if (flags & B_PROGQ) {
            GLint prog = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        }
        inst(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 1);
        if (lap)
            glDepthMask(GL_TRUE);
    }
    glFinish();
    double dt = now_ms() - t0;
    return dt / n;
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

    /* 4-8: driverens pris for de kald vores proxy selv laegger paa */
    {
        GLint dm = -1;
        glGetIntegerv(GL_DEPTH_WRITEMASK, &dm);
        printf("\n== proxyens hot path efterlignet (DEPTH_TEST=%d "
               "DEPTH_WRITEMASK=%d) ==\n",
               (int)glIsEnabled(GL_DEPTH_TEST), (int)dm);
    }
    double base = bench_variant(n, inst, 0);
    printf("instanced (reference)     : %8.3f ms/kald (%.0f kald/s)\n",
           base, 1.0 / (base / 1000.0));
    double v1 = bench_variant(n, inst, B_DEPTHQ);
    printf("  + depth-query           : %8.3f ms/kald  (tillaeg %+.3f ms)\n",
           v1, v1 - base);
    double v2 = bench_variant(n, inst, B_PROGQ);
    printf("  + program-query         : %8.3f ms/kald  (tillaeg %+.3f ms)\n",
           v2, v2 - base);
    double v3 = bench_variant(n, inst, B_MASKL);
    printf("  + mask-lap              : %8.3f ms/kald  (tillaeg %+.3f ms)\n",
           v3, v3 - base);
    double v4 = bench_variant(n, inst, B_DEPTHQ | B_PROGQ);
    printf("  + depth+program         : %8.3f ms/kald  (tillaeg %+.3f ms)\n",
           v4, v4 - base);
    double v5 = bench_variant(n, inst, B_DEPTHQ | B_PROGQ | B_MASKL);
    printf("  = hele proxy-hot-pathon : %8.3f ms/kald  (tillaeg %+.3f ms)\n",
           v5, v5 - base);
    printf("  (spillet: ~2.000-2.700 kald/frame -> tillaegget koster "
           "%.0f-%.0f ms/frame)\n",
           (v5 - base) * 2000.0, (v5 - base) * 2700.0);

    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
        printf("gl-fejl 0x%x\n", (unsigned)e);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, c);
    eglTerminate(d);
    return 0;
}
