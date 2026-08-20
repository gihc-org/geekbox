// test_triangle.cpp — vores eget GPU-program: hybris-hwcomposer-vindue + GLES2-animation.
// Kører på boksen mod vendors hybris-stak (/opt/hybris) og PowerVR G6110.
// Inspireret af libhybris' officielle test_hwcomposer.cpp (cos-mønsteret).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <hybris/hwcomposerwindow/hwcomposer.h>
#include <hybris/hwcomposerwindow/hwcomposer_window.h>
#include <android/hardware/hardware.h>
#include <android/hardware/hwcomposer.h>
#include <android/hardware/hwcomposer_defs.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define W 1920
#define H 1080

#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#endif

static hwc_composer_device_1_t *g_hwc = NULL;
static hwc_display_contents_1_t *g_dpy = NULL;

static void present_cb(void *data, struct ANativeWindow *w, struct ANativeWindowBuffer *buf)
{
    (void)data; (void)w;
    if (!g_hwc || !g_dpy) return;
    hwc_layer_1_t *layer = &g_dpy->hwLayers[0];
    g_dpy->retireFenceFd = -1;
    g_dpy->outbuf = NULL;
    g_dpy->outbufAcquireFenceFd = -1;
    g_dpy->numHwLayers = 1;
    layer->compositionType = HWC_FRAMEBUFFER_TARGET;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = buf->handle;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCropf.left = 0;
    layer->sourceCropf.top = 0;
    layer->sourceCropf.right = (float)W;
    layer->sourceCropf.bottom = (float)H;
    layer->displayFrame.left = 0;
    layer->displayFrame.top = 0;
    layer->displayFrame.right = W;
    layer->displayFrame.bottom = H;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;
    g_hwc->prepare(g_hwc, 1, &g_dpy);
    g_hwc->set(g_hwc, 1, &g_dpy);
}

static const char vertex_src[] =
    "attribute vec4 position;\n"
    "varying mediump vec2 pos;\n"
    "uniform vec4 offset;\n"
    "void main() {\n"
    "  gl_Position = position + offset;\n"
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

int main(void)
{
    printf("test_triangle: starter\n");
    fflush(stdout);

    // 1. hwc-enhed (til present-callbacken)
    const hw_module_t *hwc_mod = NULL;
    if (hw_get_module(HWC_HARDWARE_MODULE_ID, &hwc_mod) != 0) {
        fprintf(stderr, "hw_get_module(HWC) fejlede\n");
        return 1;
    }
    printf("test_triangle: hwc-modul: %s\n", hwc_mod->name ? hwc_mod->name : "?");
    if (hwc_open_1(hwc_mod, &g_hwc) != 0) {
        fprintf(stderr, "hwc_open_1 fejlede\n");
        return 1;
    }
    g_dpy = (hwc_display_contents_1_t *)calloc(1, sizeof(*g_dpy) + sizeof(hwc_layer_1_t));

    // 2. vinduet (vi laver det selv og giver det til EGL)
    struct ANativeWindow *win =
        HWCNativeWindowCreate(W, H, HAL_PIXEL_FORMAT_RGBA_8888, present_cb, NULL);
    if (!win) {
        fprintf(stderr, "HWCNativeWindowCreate fejlede\n");
        return 2;
    }
    // PVR-grallocen giver ENOMEM for HW_COMPOSER|HW_FB (0x1800) — kun HW_FB virker.
    // setUsage er protected → subclass-hack der gør den offentlig (samme vtable).
    class WindowHack : public HWComposerNativeWindow {
    public:
        WindowHack() : HWComposerNativeWindow(0, 0, 0) {}
        using HWComposerNativeWindow::setUsage;
    };
    WindowHack *obj = (WindowHack *)((char *)win - 4);
    obj->setUsage(GRALLOC_USAGE_HW_FB);
    printf("test_triangle: vindue oprettet %dx%d (usage sat til HW_FB)\n", W, H);
    fflush(stdout);

    // 3. EGL + GLES2
    EGLDisplay display = eglGetDisplay(NULL);
    if (display == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay fejlede\n"); return 3; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) { fprintf(stderr, "eglInitialize fejlede\n"); return 4; }
    printf("test_triangle: EGL %d.%d\n", major, minor);
    EGLint attr[] = { EGL_BUFFER_SIZE, 32, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(display, attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "eglChooseConfig fejlede (n=%d)\n", ncfg);
        return 5;
    }
    EGLSurface surface = eglCreateWindowSurface(display, cfg, (EGLNativeWindowType)win, NULL);
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "eglCreateWindowSurface fejlede\n"); return 6; }
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext fejlede\n"); return 7; }
    if (!eglMakeCurrent(display, surface, surface, ctx)) { fprintf(stderr, "eglMakeCurrent fejlede\n"); return 8; }

    const char *ver = (const char *)glGetString(GL_VERSION);
    const char *rend = (const char *)glGetString(GL_RENDERER);
    printf("test_triangle: GL_VERSION=%s\n", ver ? ver : "?");
    printf("test_triangle: GL_RENDERER=%s\n", rend ? rend : "?");
    fflush(stdout);

    GLuint prog = make_program();
    glUseProgram(prog);
    GLint pos_loc = glGetAttribLocation(prog, "position");
    GLint phase_loc = glGetUniformLocation(prog, "phase");
    GLint offset_loc = glGetUniformLocation(prog, "offset");
    printf("test_triangle: shader klar (loc %d %d %d)\n", pos_loc, phase_loc, offset_loc);
    fflush(stdout);

    static const GLfloat vertices[] = {
         0.0f,  1.0f, 0.0f,
        -1.0f,  0.0f, 0.0f,
         0.0f, -1.0f, 0.0f,
         1.0f,  0.0f, 0.0f,
         0.0f,  1.0f, 0.0f,
    };

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    float phase = 0.0f;
    // animér i ~8 sekunder (en frame pr. 16 ms)
    for (int i = 0; i < 500; ++i) {
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(phase_loc, phase);
        phase = fmodf(phase + 0.5f, 2.0f * 3.14159265f);
        glUniform4f(offset_loc, 0.0f, 0.0f, 0.0f, 0.0f);
        glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
        glEnableVertexAttribArray(pos_loc);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
        eglSwapBuffers(display, surface);
    }
    printf("test_triangle: FÆRDIG — 500 frames renderet\n");
    fflush(stdout);
    return 0;
}
