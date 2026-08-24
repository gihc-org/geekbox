// test_client_x11.cpp — testklient for eglplatform_x11 (BROWSER-VEJE A-prototype).
// Opretter et X-vindue, sender XID'en som EGLNativeWindowType til
// eglCreateWindowSurface og renderer cos-mønsteret med eglSwapBuffers — pixels
// præsenteres af platformen (queueBuffer → XPutImage).
//
// Kør på boksen (X kørende, GPU-stak oppe):
//   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
//   EGL_PLATFORM=x11 ./test_client_x11 --frames 120 --fps 10 --size 640x360

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* _exit overalt: libhybris/libEGL registrerer atexit ved load, og normal exit
 * kører derfor machybrisegl's refresh-display-dans (chvt + HDMI-toggle) selvom
 * vi aldrig rørte hwc — samme fælde som gles_daemon (målt 24. aug 2026). */
static void die(int code)
{
    fflush(stdout);
    _exit(code);
}

static const char vertex_src[] =
    "attribute vec4 position;\n"
    "varying mediump vec2 pos;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  pos = position.xy;\n"
    "}\n";

static const char fragment_src[] =
    "varying mediump vec2 pos;\n"
    "uniform mediump float phase;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(1., 0.9, 0.7, 1.0) *\n"
    "    cos(30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y) + atan(pos.y,pos.x) - phase);\n"
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
    int frames = 120;
    double fps = 10.0;
    int w = 640, h = 360;
    float solid_r = -1.0f, solid_g = 0.0f, solid_b = 0.0f; /* <0 = shader-demo */
    int do_xfill = 0;
    int do_xputimage = 0;
    int settle = 0;
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
        } else if (!strcmp(argv[i], "--solid") && i + 3 < argc) {
            solid_r = (float)atof(argv[++i]);
            solid_g = (float)atof(argv[++i]);
            solid_b = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--xfill")) {
            do_xfill = 1;
        } else if (!strcmp(argv[i], "--xputimage")) {
            do_xputimage = 1;
        } else if (!strcmp(argv[i], "--settle")) {
            settle = 1;
        }
    }

    setenv("EGL_PLATFORM", "x11", 1);

    const char *dname = getenv("DISPLAY");
    Display *dpy = XOpenDisplay(dname && *dname ? dname : ":0");
    if (!dpy) { fprintf(stderr, "XOpenDisplay(%s) fejlede\n", dname ? dname : ":0"); die(1); }
    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);
    Window xid = XCreateSimpleWindow(dpy, root, 120, 80, w, h, 1,
                                     BlackPixel(dpy, scr), 0x101418);
    XStoreName(dpy, xid, "eglplatform_x11 prototype — PowerVR G6110");
    XMapWindow(dpy, xid);
    XRaiseWindow(dpy, xid);
    XSelectInput(dpy, xid, ExposureMask);
    XFlush(dpy);
    if (do_xfill) {
        /* Diagnostik: fyld vinduet grønt (0x07E0 = gyldig RGB565-pixel) fra
         * klientens EGEN X-forbindelse — sammenlignes med platformens røde
         * fyld for at isolere kors-forbindelses-tegning. */
        XSetForeground(dpy, DefaultGC(dpy, scr), 0x07E0);
        XFillRectangle(dpy, xid, DefaultGC(dpy, scr), 0, 0, w, h);
        XSync(dpy, False);
        printf("test_client_x11: egen XFillRectangle (grøn) sendt\n");
        fflush(stdout);
    }
    /* dræn Expose/MapNotify kort — sikrer serveren har tegnet vinduet */
    {
        struct timespec ts = { 0, 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
        }
    }
    if (do_xputimage) {
        /* window_demo-identisk XPutImage: 16-bit ZPixmap, grøn helt fyld */
        int depth16 = DefaultDepth(dpy, scr);
        std::vector<unsigned char> px((size_t)w * h * 2);
        for (size_t i = 0; i < px.size(); i += 2) {
            px[i] = 0xE0;
            px[i + 1] = 0x07; /* 0x07E0 grøn */
        }
        XImage *img = XCreateImage(dpy, DefaultVisual(dpy, scr), depth16,
                                   ZPixmap, 0, (char *)&px[0], w, h, 16, w * 2);
        if (!img) {
            fprintf(stderr, "XCreateImage fejlede\n");
            die(1);
        }
        XPutImage(dpy, xid, DefaultGC(dpy, scr), img, 0, 0, 0, 0, w, h);
        XFlush(dpy);
        img->data = NULL;
        XDestroyImage(img);
        printf("test_client_x11: egen XPutImage (grøn) sendt\n");
        fflush(stdout);
    }
    if (settle) {
        /* vent på at openbox er færdig med at konfigurere vinduet + dræn
         * Expose-hændelser — ellers males vinduets baggrund hen over tidlig
         * tegning (målt med xdraw_probe, 24. aug 2026) */
        struct timespec ts = { 2, 0 };
        nanosleep(&ts, NULL);
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
        }
        printf("test_client_x11: settle færdig\n");
        fflush(stdout);
    }
    /* læs vinduet tilbage fra serveren (kun med --xputimage) */
    if (do_xputimage) {
        XImage *back = XGetImage(dpy, xid, 0, 0, w, h, AllPlanes, ZPixmap);
        if (back) {
            unsigned long v = 0;
            if (back->bits_per_pixel == 16)
                v = ((unsigned short *)back->data)[0];
            printf("test_client_x11: XGetImage(0,0) efter tegning = 0x%04lx\n", v);
            XDestroyImage(back);
        } else {
            printf("test_client_x11: XGetImage fejlede\n");
        }
        fflush(stdout);
    }
    printf("test_client_x11: X-vindue 0x%lx (%dx%d)\n", (unsigned long)xid, w, h);
    fflush(stdout);

    /* Send vores Display* som EGL-native-display: platformen skal tegne via
     * DENNE forbindelse (kors-forbindelses-tegning når ikke fb0, målt) */
    EGLDisplay display = eglGetDisplay((EGLNativeDisplayType)dpy);
    if (display == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay fejlede\n"); die(2); }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) { fprintf(stderr, "eglInitialize fejlede\n"); die(3); }
    printf("test_client_x11: EGL %d.%d\n", major, minor);
    EGLint attr[] = { EGL_BUFFER_SIZE, 32, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(display, attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "eglChooseConfig fejlede (n=%d)\n", ncfg);
        die(4);
    }
    EGLSurface surface = eglCreateWindowSurface(
        display, cfg, (EGLNativeWindowType)(uintptr_t)xid, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface fejlede (EGL error 0x%x)\n",
                eglGetError());
        die(5);
    }
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext fejlede\n"); die(6); }
    if (!eglMakeCurrent(display, surface, surface, ctx)) {
        fprintf(stderr, "eglMakeCurrent fejlede\n");
        die(7);
    }
    printf("test_client_x11: GL_VERSION=%s\n",
           (const char *)glGetString(GL_VERSION));
    printf("test_client_x11: GL_RENDERER=%s\n",
           (const char *)glGetString(GL_RENDERER));
    fflush(stdout);

    GLuint prog = 0;
    GLint pos_loc = -1, phase_loc = -1;
    static const GLfloat vertices[] = {
         0.0f,  1.0f, 0.0f,
        -1.0f,  0.0f, 0.0f,
         0.0f, -1.0f, 0.0f,
         1.0f,  0.0f, 0.0f,
         0.0f,  1.0f, 0.0f,
    };
    if (solid_r < 0.0f) {
        prog = make_program();
        glUseProgram(prog);
        pos_loc = glGetAttribLocation(prog, "position");
        phase_loc = glGetUniformLocation(prog, "phase");
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glClearColor(solid_r, solid_g, solid_b, 1.0f);
    }
    double t0 = now_ms();
    double phase = 0.0;
    double step = 6.283185307179586 / (frames > 0 ? frames : 1);
    for (int i = 0; i < frames; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        if (solid_r < 0.0f) {
            glUniform1f(phase_loc, (float)phase);
            phase += step;
            glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
            glEnableVertexAttribArray(pos_loc);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
        }
        eglSwapBuffers(display, surface);
        double delay = t0 + (i + 1) * (1000.0 / fps) - now_ms();
        if (delay > 0)
            usleep((useconds_t)(delay * 1000.0));
    }
    double t_el = now_ms() - t0;
    printf("test_client_x11: færdig — %d frames på %.1f s (%.1f fps)\n",
           frames, t_el / 1000.0, t_el > 0 ? frames / (t_el / 1000.0) : 0.0);
    fflush(stdout);

    eglDestroySurface(display, surface);
    eglDestroyContext(display, ctx);
    eglTerminate(display);
    XDestroyWindow(dpy, xid);
    XCloseDisplay(dpy);
    _exit(0); /* spring over hybris' atexit-dans — samme fælde som daemonen */
}
