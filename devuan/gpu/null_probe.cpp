// null_probe.cpp — probe af EGL_PLATFORM=null (BROWSER-VEJE.md B7, aug 2026).
//
// Formål: afgøre om null-platformen giver en offscreen-kontekst der kan
// (a) oprette en overflade med nul-vindue (eglCreateWindowSurface + NULL),
// (b) rendere i et FBO + glReadPixels, og
// (c) rendere direkte i default-framebufferen + glReadPixels (uden swap).
// Ingen hwcomposer, ingen skærm-berøring — og dermed ingen strøm-cyklus.
//
// Kør (GPU-stak oppe): EGL_PLATFORM=null LD_LIBRARY_PATH=/opt/hybris \
//   LD_PRELOAD=/root/system_shim.so /root/null_probe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define W 1920
#define H 1080

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER            0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0      0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#endif
extern void glGenFramebuffers(GLsizei n, GLuint *ids);
extern void glBindFramebuffer(GLenum target, GLuint fb);
extern void glFramebufferTexture2D(GLenum target, GLenum attachment,
                                   GLenum textarget, GLuint texture, GLint level);
extern GLenum glCheckFramebufferStatus(GLenum target);

static const char vertex_src[] =
    "attribute vec4 position;\n"
    "void main() { gl_Position = position; }\n";
static const char fragment_src[] =
    "uniform mediump vec4 color;\n"
    "void main() { gl_FragColor = color; }\n";

static GLuint make_program(void)
{
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const GLchar *vs_src = vertex_src;
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar *fs_src = fragment_src;
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    return prog;
}

static void draw_triangle(GLuint prog)
{
    static const GLfloat v[] = { 0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f };
    GLint pos = glGetAttribLocation(prog, "position");
    GLint col = glGetUniformLocation(prog, "color");
    glUseProgram(prog);
    glUniform4f(col, 1.0f, 0.2f, 0.2f, 1.0f);
    glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, 0, v);
    glEnableVertexAttribArray(pos);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
}

/* tjek at readback ikke er tomt/hvidt: tæl afvigelser fra glClear-farven */
static int pixels_changed(const unsigned char *buf, size_t n, int r, int g, int b)
{
    size_t changed = 0;
    for (size_t i = 0; i < n; i += 4) {
        if (buf[i] != r || buf[i + 1] != g || buf[i + 2] != b)
            changed++;
    }
    return (int)changed;
}

int main(void)
{
    printf("null_probe: starter (EGL_PLATFORM=%s)\n",
           getenv("EGL_PLATFORM") ? getenv("EGL_PLATFORM") : "(ikke sat)");
    fflush(stdout);

    EGLDisplay dpy = eglGetDisplay(NULL);
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay fejlede\n"); return 1; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) { fprintf(stderr, "eglInitialize fejlede\n"); return 1; }
    EGLint attr[] = { EGL_BUFFER_SIZE, 32, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "eglChooseConfig fejlede (n=%d)\n", ncfg);
        return 1;
    }
    printf("null_probe: EGL %d.%d\n", major, minor);

    /* Nul-vindue: null-platformen laver selv en offscreen-overflade */
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)NULL, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface(NULL) fejlede (EGL-error 0x%x)\n", eglGetError());
        return 1;
    }
    printf("null_probe: window-surface med NULL vindue OK\n");
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext fejlede\n"); return 1; }
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
        fprintf(stderr, "eglMakeCurrent fejlede\n");
        return 1;
    }
    printf("null_probe: GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
    printf("null_probe: GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    fflush(stdout);

    GLuint prog = make_program();
    unsigned char *buf = (unsigned char *)malloc((size_t)W * H * 4);
    if (!buf) { fprintf(stderr, "malloc fejlede\n"); return 1; }

    /* Stage A: FBO */
    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO ukomplet (0x%x) — stop\n", st);
        return 2;
    }
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_triangle(prog);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    int changed = pixels_changed(buf, (size_t)W * H * 4, 0, 0, 255);
    printf("null_probe: FBO readback OK — %d pixels afviger fra blå baggrund\n", changed);

    /* Stage B: default framebuffer (ingen swap) */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_triangle(prog);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    changed = pixels_changed(buf, (size_t)W * H * 4, 0, 255, 0);
    printf("null_probe: default-framebuffer readback OK — %d pixels afviger fra grøn baggrund\n", changed);

    printf("null_probe: FÆRDIG — begge veje kørte uden exit(42)\n");
    return 0;
}
