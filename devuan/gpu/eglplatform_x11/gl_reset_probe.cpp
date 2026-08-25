// gl_reset_probe.cpp — standalone GLES2-probe der renderer + eglSwapBuffers
// i N frames og hver frame tjekker glGetError()/glGetGraphicsResetStatus()/
// eglGetError() — for at afgøre om vendor-GL sporadisk melder fejl/reset
// (det Firefox' WR_POST_UPDATE-detektion kan fange).
//
// Kør på boksen (X kørende, GPU-stak oppe):
//   DISPLAY=:0 LD_PRELOAD="/usr/local/lib/firefox-webgl/system_shim.so \
//     /usr/local/lib/firefox-webgl/egl_platform_shim.so" \
//   LD_LIBRARY_PATH=/opt/hybris:/usr/local/lib/firefox-webgl \
//   EGL_PLATFORM=x11 ./gl_reset_probe --frames 300 --size 1280x720
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

typedef unsigned int (*glGetGraphicsResetStatus_t)(void);

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void die(int code)
{
    fflush(stdout);
    _exit(code);
}

static const char vertex_src[] =
    "attribute vec4 position;\n"
    "void main(){ gl_Position = position; }\n";
static const char fragment_src[] =
    "precision mediump float;\n"
    "uniform mediump float phase;\n"
    "void main(){\n"
    "  gl_FragColor = vec4(0.2 + 0.8*abs(sin(phase)),\n"
    "                      0.2 + 0.8*abs(cos(phase*1.3)), 0.5, 1.0);\n"
    "}\n";

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

int main(int argc, char **argv)
{
    int frames = 300;
    double fps = 10.0;
    int w = 1280, h = 720;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc)
            fps = atof(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &w, &h) != 2) {
                fprintf(stderr, "--size skal være WxH\n");
                die(1);
            }
        }
    }

    setenv("EGL_PLATFORM", "x11", 1);
    Display *dpy = XOpenDisplay(getenv("DISPLAY") ? getenv("DISPLAY") : ":0");
    if (!dpy) { fprintf(stderr, "XOpenDisplay fejlede\n"); die(1); }
    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);
    Window xid = XCreateSimpleWindow(dpy, root, 120, 80, w, h, 1,
                                     BlackPixel(dpy, scr), 0x101418);
    XStoreName(dpy, xid, "gl_reset_probe");
    XMapWindow(dpy, xid);
    XRaiseWindow(dpy, xid);
    XSync(dpy, False);
    {
        struct timespec ts = { 2, 0 }; /* settle: openbox + dræn Expose */
        nanosleep(&ts, NULL);
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
        }
    }

    EGLDisplay display = eglGetDisplay((EGLNativeDisplayType)dpy);
    if (display == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay fejlede\n"); die(2); }
    if (!eglInitialize(display, NULL, NULL)) { fprintf(stderr, "eglInitialize fejlede\n"); die(3); }
    EGLint attr[] = { EGL_BUFFER_SIZE, 32, EGL_RENDERABLE_TYPE,
                      EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(display, attr, &cfg, 1, &ncfg) || ncfg < 1) { die(4); }
    EGLSurface surface = eglCreateWindowSurface(
        display, cfg, (EGLNativeWindowType)(uintptr_t)xid, NULL);
    if (surface == EGL_NO_SURFACE) { die(5); }
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) { die(6); }
    if (!eglMakeCurrent(display, surface, surface, ctx)) { die(7); }

    printf("GL_VERSION=%s\nGL_RENDERER=%s\n",
           (const char *)glGetString(GL_VERSION),
           (const char *)glGetString(GL_RENDERER));
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    printf("robustness-i-extensions: %s\n",
           exts && strstr(exts, "robustness") ? "ja" : "nej");
    glGetGraphicsResetStatus_t getReset =
        (glGetGraphicsResetStatus_t)eglGetProcAddress("glGetGraphicsResetStatus");
    printf("glGetGraphicsResetStatus: %s\n", getReset ? "tilgængelig" : "ikke tilgængelig");
    fflush(stdout);

    GLuint prog = make_program();
    glUseProgram(prog);
    GLint pos = glGetAttribLocation(prog, "position");
    GLint phaseLoc = glGetUniformLocation(prog, "phase");
    static const GLfloat verts[] = {
         0.0f,  1.0f, 0.0f,
        -1.0f,  0.0f, 0.0f,
         0.0f, -1.0f, 0.0f,
         1.0f,  0.0f, 0.0f,
         0.0f,  1.0f, 0.0f,
    };
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    double t0 = now_ms();
    int errs = 0;
    for (int i = 0; i < frames; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(phaseLoc, (float)i * 0.05f);
        glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, 0, verts);
        glEnableVertexAttribArray(pos);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);

        EGLBoolean ok = eglSwapBuffers(display, surface);
        EGLint eglErr = eglGetError();
        GLenum glErr = glGetError();
        unsigned int reset = 0;
        if (getReset)
            reset = getReset();
        if (!ok || eglErr != EGL_SUCCESS || glErr != GL_NO_ERROR || reset != 0) {
            printf("frame %d: swap=%d eglErr=0x%x glErr=0x%x reset=0x%x\n",
                   i, ok, eglErr, glErr, reset);
            errs++;
        }
        double delay = t0 + (i + 1) * (1000.0 / fps) - now_ms();
        if (delay > 0)
            usleep((useconds_t)(delay * 1000.0));
    }
    printf("færdig: %d frames, %d afvigelser\n", frames, errs);
    fflush(stdout);
    _exit(0);
}
