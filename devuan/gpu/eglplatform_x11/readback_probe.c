/* readback_probe.c - hvor dyr er glReadPixels paa 1.5-stakken?
 *
 * Baggrund (17. sep 2026): med layers.acceleration.disabled=true (software-
 * layers) skal Firefox kopiere WebGL-canvas'et til en software-flade for hver
 * composite. Maalt i dag: hele browseren kører ~1,5-2,6 fps og skaermen
 * opdaterer ~0-1,6 gange/s, mens spillets GL-rendering er i orden. Hvis en
 * enkelt readback af canvas-stoerrelsen (836x470 RGBA) tager hundredvis af
 * millisekunder, ER det forklaringen paa den lave fps - og dermed ikke
 * spillet, proxyen eller instrumenteringen.
 *
 * Byg/koer paa boksen:
 *   gcc -O2 -o /root/readback_probe /root/readback_probe.c \
 *       -I/usr/local/include -L/opt/hybris -Wl,--no-as-needed \
 *       -l:libEGL_r.so -l:libGLESv2.so.2 -lhybris-common \
 *       -landroid-properties -ldl -lrt -lm
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *       EGL_PLATFORM=x11 DISPLAY=:0 /root/readback_probe
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static GLuint setup_fbo(int w, int h)
{
    GLuint fbo, tex, rb;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, rb);
    glViewport(0, 0, w, h);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("  advarsel: FBO ikke komplet (%dx%d)\n", w, h);
    return fbo;
}

static void bench(const char *name, int w, int h, int iters)
{
    unsigned char *buf = malloc((size_t)w * h * 4);
    if (!buf)
        return;
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    double t0 = now_ms();
    for (int i = 0; i < iters; i++)
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    double dt = now_ms() - t0;
    printf("%-28s %4dx%-5d %3d iters: %7.1f ms/readback -> maks %.1f fps\n",
           name, w, h, iters, dt / iters, 1000.0 / (dt / iters));
    /* uden glFinish ser vi kun kø-tiden; med Finish tvinges synkronisering */
    t0 = now_ms();
    for (int i = 0; i < iters; i++) {
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
        glFinish();
    }
    dt = now_ms() - t0;
    printf("%-28s %4dx%-5d %3d iters: %7.1f ms/readback -> maks %.1f fps "
           "(inkl. glFinish)\n",
           name, w, h, iters, dt / iters, 1000.0 / (dt / iters));
    free(buf);
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
        printf("  gl-fejl 0x%x\n", (unsigned)e);
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
    printf("GL_VERSION: %s\nGL_RENDERER: %s\n\n",
           glGetString(GL_VERSION), glGetString(GL_RENDERER));

    printf("== glReadPixels-pris (FBO, RGBA/UNSIGNED_BYTE) ==\n");
    setup_fbo(836, 470);
    bench("canvas 836x470", 836, 470, 10);
    setup_fbo(1920, 1080);
    bench("skaerm 1920x1080", 1920, 1080, 5);
    setup_fbo(300, 150);
    bench("lille 300x150", 300, 150, 20);

    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, c);
    eglTerminate(d);
    return 0;
}
