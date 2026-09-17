/* depthclear_probe.c — isoleret test af glClearDepthf på DDK 1.5-stakken.
 *
 * Baggrund (cyan-scene, 8. sep 2026): spillets verdens-draws fejlede LEQUAL
 * mod en depth-buffer der tilsyneladende var ryddet til 0 — selv om spillet
 * kalder glClearDepthf(1) (proxy-log: "clearDepthf d=1 (kald=1..6)") og
 * glGetFloatv(GL_DEPTH_CLEAR_VALUE) returnerer skrald (7,1e-44). Først da
 * proxyen selv kaldte rgl_glClearDepthf(1.0f) umiddelbart før glClear virkede
 * verdens-draws (vnext11). Spørgsmålet er HVORFOR den tidligere satte værdi
 * ikke slår igennem: er driverens default 0, bliver værdien tabt ved
 * flush/frame/bind, eller er glClearDepthf en no-op?
 *
 * Denne probe måler effekten i stedet for at spørge driveren (query er
 * ubrugelig): et quad tegnes i z=0,5 med DEPTH_TEST=LESS mod en depth-buffer
 * der er ryddet på forskellige måder. Er quad'et synligt, var den effektive
 * clear-værdi > 0,5 (dvs. 1); er det usynligt, var den ≈ 0.
 *
 * Byg/kør på boksen:
 *   gcc -O0 -g -o /root/depthclear_probe /root/depthclear_probe.c \
 *       -I/usr/local/include -L/opt/hybris -Wl,--no-as-needed \
 *       -l:libEGL_r.so -l:libGLESv2.so.2 -lhybris-common \
 *       -landroid-properties -ldl -lrt -lm
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *       EGL_PLATFORM=x11 DISPLAY=:0 /root/depthclear_probe
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *VS =
    "attribute vec3 aPos;\n"
    "void main() { gl_Position = vec4(aPos, 1.0); }\n";
static const char *FS =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n";

static const int W = 256, H = 144;
static GLuint g_fbo = 0, g_tex = 0, g_depth = 0;
static GLuint g_prog = 0, g_vbo = 0, g_attr = 0;
static int g_gl_err_seen = 0;

static void check_err(const char *where)
{
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) {
        printf("    GL-FEJL 0x%x ved %s\n", (unsigned)e, where);
        g_gl_err_seen = 1;
    }
}

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

static void setup_fbo(GLuint *fbo, GLuint *tex, GLuint *depth)
{
    glGenFramebuffers(1, fbo);
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, depth);
    glBindRenderbuffer(GL_RENDERBUFFER, *depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, *tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, *depth);
    glViewport(0, 0, W, H);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
        printf("  FBO-status 0x%x (IKKE komplet)\n", (unsigned)st);
}

/* Tegner quad'et i z=0,5 og tæller grønne pixels (dvs. "slap igennem
 * depth-testen"). */
static int draw_and_count(void)
{
    static unsigned char *px = NULL;
    if (!px)
        px = malloc((size_t)W * H * 4);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();
    memset(px, 0, (size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int n = 0;
    for (int y = 0; y < H; y += 2)
        for (int x = 0; x < W; x += 2) {
            unsigned char *q = px + ((size_t)y * W + x) * 4;
            if (q[1] > 128)
                n++;
        }
    return n;
}

static int samples_total(void)
{
    return (W / 2) * (H / 2);
}

/* Rydder color+depth og tegner. returnerer antal pixels der slap igennem. */
static int clear_and_draw(GLbitfield mask)
{
    glClear(mask);
    return draw_and_count();
}

static void report(const char *name, int visible, const char *note)
{
    printf("%-34s synlige=%5d/%-5d %-9s %s\n", name, visible,
           samples_total(),
           visible > samples_total() / 2 ? "SLAP-IGENNEM" : "BLOKERET",
           note ? note : "");
    check_err(name);
}

static void unrelated_calls(int n)
{
    GLuint tmp;
    glGenBuffers(1, &tmp);
    for (int i = 0; i < n; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, tmp);
        glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glUseProgram(g_prog);
        glViewport(0, 0, W, H);
        glActiveTexture(GL_TEXTURE0);
    }
    glDeleteBuffers(1, &tmp);
}

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, NULL, NULL);
    EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    eglChooseConfig(d, a, &cfg, 1, &n);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (c == EGL_NO_CONTEXT) {
        fprintf(stderr, "EGL ES3-kontekst fejlede\n");
        return 1;
    }
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
    printf("GL_VERSION: %s\nGL_RENDERER: %s\n",
           glGetString(GL_VERSION), glGetString(GL_RENDERER));

    setup_fbo(&g_fbo, &g_tex, &g_depth);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, make_shader(GL_VERTEX_SHADER, VS));
    glAttachShader(g_prog, make_shader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(g_prog);
    glUseProgram(g_prog);
    g_attr = glGetAttribLocation(g_prog, "aPos");
    /* quad i z=0,5 (x,y i [-1,1]) */
    GLfloat v[4 * 3] = { -1, -1, 0.5f, 1, -1, 0.5f, -1, 1, 0.5f, 1, 1, 0.5f };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)g_attr);
    glVertexAttribPointer((GLuint)g_attr, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    check_err("opsætning");

    printf("\n== effektiv depth-clear-værdi (quad i z=0,5, DEPTH_TEST=LESS) ==\n");

    /* T0: ingen glClearDepthf nogensinde — hvad er driverens default? */
    report("T0 default (ingen clearDepthf)",
           clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
           "GL-default skal give synligt quad (>0,5)");

    /* T1: eksplicit 1,0 lige før clear (den spillbare opskrift). */
    glClearDepthf(1.0f);
    report("T1 clearDepthf(1) lige før clear",
           clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
           "skal give synligt quad");

    /* T2: 0,0 — følsomheds-tjek af målingen. */
    glClearDepthf(0.0f);
    report("T2 clearDepthf(0) lige før clear",
           clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
           "skal give 0 synlige");

    /* T3: findes/ virker glClearDepth (double-udgaven)? Den er ikke en del af
     * GLES-headeren, så den hentes via eglGetProcAddress. */
    {
        void (*pClearDepth)(double) =
            (void (*)(double))eglGetProcAddress("glClearDepth");
        printf("T3  glClearDepth-pointer via eglGetProcAddress: %p\n",
               (void *)pClearDepth);
        if (pClearDepth) {
            pClearDepth(1.0);
            report("T3 clearDepth(1.0) (double)",
                   clear_and_draw(GL_COLOR_BUFFER_BIT |
                                  GL_DEPTH_BUFFER_BIT),
                   "double-udgaven af samme kald");
        } else {
            printf("%-34s findes ikke paa stakken - springes over\n",
                   "T3 clearDepth(1.0)");
        }
    }

    /* T4: holder værdien over flere frames (samme kontekst, ingen ny sætning)? */
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (int i = 1; i <= 3; i++) {
        char nm[64];
        snprintf(nm, sizeof nm, "T4 frame %d efter ét clearDepthf(1)", i);
        report(nm, clear_and_draw(GL_COLOR_BUFFER_BIT |
                                  GL_DEPTH_BUFFER_BIT),
               "værdien skal holde mellem frames");
    }

    /* T5: holder værdien efter mange ikke-relaterede GL-kald (flush-mistanke)? */
    glClearDepthf(1.0f);
    unrelated_calls(200);
    report("T5 efter 200 ikke-relaterede kald",
           clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
           "kald der ikke rører clear-state");

    /* T6: holder værdien over et eglMakeCurrent-skift? */
    glClearDepthf(1.0f);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
    report("T6 efter eglMakeCurrent-skift",
           clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
           "kontekst-state skal bevares");

    /* T7: kun depth-bittet ryddet (som proxyens force-clear) — farven ryddes
     * separat til sort først, så et grønt quad betyder "slap igennem". */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    report("T7 kun DEPTH-bittet ryddet", draw_and_count(),
           "farve ryddet separat først");

    /* T8: taber værdien sig ved et framebuffer-bind? */
    glClearDepthf(1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    report("T8 efter bind 0 -> fbo igen",
           clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
           "værdien skal overleve bind");

    /* T9: frisk FBO — er default-værdien per-FBO? */
    {
        GLuint f2, t2, d2;
        setup_fbo(&f2, &t2, &d2);
        report("T9 frisk FBO, ingen clearDepthf",
               clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
               "default på et nyt FBO");
        glClearDepthf(1.0f);
        report("T10 frisk FBO + clearDepthf(1)",
               clear_and_draw(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
               "samme FBO med eksplicit 1,0");
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    }

    /* T11/T12: hvad siger query'en (forventet ubrugelig på 1.5)? */
    glClearDepthf(1.0f);
    {
        GLfloat v1 = -123.0f;
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &v1);
        glClearDepthf(0.0f);
        GLfloat v0 = -123.0f;
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &v0);
        printf("T11 GL_DEPTH_CLEAR_VALUE-query: efter 1,0 -> %g ; efter 0,0 "
               "-> %g  (skrald = query er ubrugelig)\n", (double)v1,
               (double)v0);
        check_err("query");
    }

    /* T12/T13/T14: DELER DRIVEREN CLEAR-DEPTH MELLEM KONTEKSTER?
     * Live-mønsteret er at spillets glClearDepthf(1) (kaldt få gange tidligt)
     * ikke slår igennem, mens et kald umiddelbart før glClear gør. Firefox har
     * flere WebGL-kontekster i samme proces, så hvis clear-værdien er global
     * (eller tabes ved kontekstskift), er det forklaringen. */
    {
        EGLContext c2 = eglCreateContext(d, cfg, c, ca);
        GLuint f2 = 0, t2 = 0, d2 = 0;
        if (c2 == EGL_NO_CONTEXT) {
            printf("T12 kunne ikke oprette en anden kontekst - springes over\n");
        } else {
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c2);
            setup_fbo(&f2, &t2, &d2);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            check_err("kontekst B opsætning");

            /* T12: A sætter 1,0 -> B skifter til 0 -> tilbage til A */
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
            glClearDepthf(1.0f);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c2);
            glClearDepthf(0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
            report("T12 B satte 0,0 imens A havde 1,0",
                   clear_and_draw(GL_COLOR_BUFFER_BIT |
                                  GL_DEPTH_BUFFER_BIT),
                   "SLAP-IGENNEM = per-kontekst (korrekt)");

            /* T13: A sætter 1,0 -> B rører ikke clear-state -> tilbage */
            glClearDepthf(1.0f);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c2);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
            report("T13 B ryddede uden clearDepthf-kald",
                   clear_and_draw(GL_COLOR_BUFFER_BIT |
                                  GL_DEPTH_BUFFER_BIT),
                   "SLAP-IGENNEM = værdien holdt");

            /* T14: A sætter 1,0 -> B bliver current og laver arbejde ->
             * tilbage til A og ryd (uden at B rører depth-clear) */
            glClearDepthf(1.0f);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c2);
            glUseProgram(g_prog);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
            glBindFramebuffer(GL_FRAMEBUFFER, f2);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            report("T14 B arbejdede, A rydder bagefter",
                   clear_and_draw(GL_COLOR_BUFFER_BIT |
                                  GL_DEPTH_BUFFER_BIT),
                   "SLAP-IGENNEM = værdien holdt");
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(d, c2);
            eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
        }
    }

    printf("\nSammenfatning: se hvilke linjer der giver SLAP-IGENNEM. "
           "gl-fejl set: %d\n", g_gl_err_seen);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, c);
    eglTerminate(d);
    return 0;
}
