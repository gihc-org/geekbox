// readback_probe.cpp — finder ud af hvilken glReadPixels-vej der virker (aug 2026).
//
// Baggrund: gles_daemon's FBO-readback gav SIGSEGV (si_addr=NULL) inde i den gamle
// PVR-DDK (1.4@3632227) — set med strace. Spørgsmålet er om readback fra
// DEFAULT-framebufferen (hwcomposer-vinduesfladen, UDEN eglSwapBuffers) virker.
//
// Proben bygger på test_triangle.cpp's init (hwc-vindue + EGL + GLES2), renderer
// til framebuffer 0, kalder glFinish + glReadPixels og tjekker at pixels afviger
// fra baggrunden. Til sidst forsøges FBO-vejen (forventet crash — dokumenterer
// fejlen i samme kørsel).
//
// Kør (X stoppet, GPU-stak oppe):
//   EGL_PLATFORM=hwcomposer LD_LIBRARY_PATH=/opt/hybris \
//   LD_PRELOAD=/root/system_shim.so /root/readback_probe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <hybris/dlfcn/dlfcn.h>
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

static hwc_composer_device_1_t *g_hwc = NULL;
static hwc_display_contents_1_t *g_dpy = NULL;

static void present_cb(void *data, struct ANativeWindow *w,
                       struct ANativeWindowBuffer *buf)
{
    (void)data; (void)w; (void)buf;   /* vi swapper aldrig */
}

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

static int pixels_changed(const unsigned char *buf, size_t n, int r, int g, int b)
{
    size_t changed = 0;
    for (size_t i = 0; i < n; i += 4) {
        if (buf[i] != r || buf[i + 1] != g || buf[i + 2] != b)
            changed++;
    }
    return (int)changed;
}

/*
 * Wrapperens _glReadPixels-slot (BSS, offset 0x101dc fundet via disassembly af
 * glReadPixels_wrapper) er NULL i libGLESv2.so.2 — init'ens android_dlsym løste
 * symbolet ikke. Her resolver vi den ægte DDK-funktion via hybris' android-linker
 * og skriver den ind i slottet, så wrapperen virker.
 */
static void patch_readpixels(void)
{
    Dl_info info;
    if (!dladdr((void *)&glClear, &info) || !info.dli_fbase) {
        fprintf(stderr, "readback_probe: dladdr(libGLESv2) fejlede\n");
        return;
    }
    void **slot = (void **)((char *)info.dli_fbase + 0x101dc);
    void *h = hybris_dlopen("libGLESv2.so", 0);
    if (!h) {
        fprintf(stderr, "readback_probe: hybris_dlopen(libGLESv2.so) fejlede\n");
        return;
    }
    void *real = hybris_dlsym(h, "glReadPixels");
    if (!real) {
        fprintf(stderr, "readback_probe: hybris_dlsym(glReadPixels) fejlede\n");
        return;
    }
    printf("readback_probe: patcher _glReadPixels-slot %p <- %p\n", slot, real);
    *slot = real;
}

int main(void)
{
    printf("readback_probe: starter\n");
    fflush(stdout);

    const hw_module_t *hwc_mod = NULL;
    if (hw_get_module(HWC_HARDWARE_MODULE_ID, &hwc_mod) != 0) {
        fprintf(stderr, "hw_get_module(HWC) fejlede\n");
        return 1;
    }
    if (hwc_open_1(hwc_mod, &g_hwc) != 0) {
        fprintf(stderr, "hwc_open_1 fejlede\n");
        return 1;
    }
    g_dpy = (hwc_display_contents_1_t *)calloc(1, sizeof(*g_dpy) + sizeof(hwc_layer_1_t));
    struct ANativeWindow *win =
        HWCNativeWindowCreate(W, H, HAL_PIXEL_FORMAT_RGBA_8888, present_cb, NULL);
    if (!win) {
        fprintf(stderr, "HWCNativeWindowCreate fejlede\n");
        return 2;
    }
    class WindowHack : public HWComposerNativeWindow {
    public:
        WindowHack() : HWComposerNativeWindow(0, 0, 0) {}
        using HWComposerNativeWindow::setUsage;
    };
    WindowHack *obj = (WindowHack *)((char *)win - 4);
    /* SW_READ_OFTEN er mistænkt nødvendig for glReadPixels' interne map */
    obj->setUsage(GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_SW_READ_OFTEN);
    printf("readback_probe: usage = 0x%x (HW_FB|SW_READ_OFTEN)\n",
           (unsigned)GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_SW_READ_OFTEN);

    EGLDisplay dpy = eglGetDisplay(NULL);
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay fejlede\n"); return 3; }
    if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "eglInitialize fejlede\n"); return 4; }
    EGLint attr[] = { EGL_BUFFER_SIZE, 32, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "eglChooseConfig fejlede (n=%d)\n", ncfg);
        return 5;
    }
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)win, NULL);
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "eglCreateWindowSurface fejlede\n"); return 6; }
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext fejlede\n"); return 7; }
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
        fprintf(stderr, "eglMakeCurrent fejlede\n");
        return 8;
    }
    printf("readback_probe: GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
    printf("readback_probe: GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    fflush(stdout);

    patch_readpixels();

    GLuint prog = make_program();
    unsigned char *buf = (unsigned char *)malloc((size_t)W * H * 4);
    if (!buf) { fprintf(stderr, "malloc fejlede\n"); return 9; }

    /* Stage B: DEFAULT framebuffer — render + glReadPixels UDEN swap */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_triangle(prog);
    printf("readback_probe: tegnet i default-framebuffer, kalder glReadPixels...\n");
    fflush(stdout);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    int changed = pixels_changed(buf, (size_t)W * H * 4, 0, 255, 0);
    printf("readback_probe: DEFAULT-readback OK — %d pixels afviger fra grøn\n", changed);
    fflush(stdout);

    /* Stage A: FBO (forventet crash — dokumenterer fejlen) */
    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("readback_probe: FBO-status 0x%x\n", st);
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_triangle(prog);
    printf("readback_probe: tegnet i FBO, kalder glReadPixels...\n");
    fflush(stdout);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    changed = pixels_changed(buf, (size_t)W * H * 4, 0, 0, 255);
    printf("readback_probe: FBO-readback OK — %d pixels afviger fra blå\n", changed);

    printf("readback_probe: FÆRDIG — begge veje virker\n");
    return 0;
}
