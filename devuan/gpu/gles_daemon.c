/*
 * gles_daemon.c — GLES-daemon (M1, aug 2026).
 *
 * Arkitektur (se GLES-DAEMON-PLAN.md): Python-frontend tegner UI på /dev/fb0 og
 * sender JSON-linjer over en unix-socket; denne daemon renderer GLES-scener
 * OFFSCREEN (FBO) og blitter resultatet til fb0.
 *
 * Bygger på test_triangle.cpp (hybris-hwcomposer-vindue + EGL + GLES2) — men der
 * kaldes ALDRIG eglSwapBuffers: vinduet er kun EGL-"current"-holder, renderingen
 * sker i et FBO, og glReadPixels → fb0. Dermed kører hwc-præsentationen aldrig.
 *
 * Protokol (v1): JSON-linjer, synkron. Kommandoer:
 *   {"cmd":"ping"}
 *   {"cmd":"fb"}
 *   {"cmd":"scenes"}
 *   {"cmd":"render","scene":"triangle","rect":[x,y,w,h],"phase":0.5}
 *       — render + blit til fb0 (kiosk-tilstand; X skal være stoppet)
 *   {"cmd":"frame","scene":"triangle","rect":[x,y,w,h],"phase":0.5}
 *       — render og RETURNER rå pixels: først en JSON-header-linje
 *         {"ok":true,"w":..,"h":..,"fmt":"rgba8|rgb565","bytes":N}, derefter
 *         N bytes (GL-orientering: række 0 = bund). fmt="rgb565" (2 bytes/px,
 *         little-endian R5G6B5) sparer frontenden for Python-pakning.
 *         Til X-vindue-frontends.
 *   {"cmd":"clear","rect":[x,y,w,h],"color":[r,g,b]}        // 0-255
 *   {"cmd":"quit"}
 * Svar: {"ok":true/false, ...}; fejl: {"ok":false,"error":"..."}.
 *
 * Kør (X skal være stoppet):
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *   EGL_PLATFORM=hwcomposer /root/gles_daemon [/tmp/gles.sock]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fb.h>

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
#define SOCK_PATH_DEFAULT "/tmp/gles.sock"
#define CMD_MAX 8192

#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#endif

/* --- GLES3-FBO (driveren er ES3.1, men vendor-gl2.h har kun ES2-kernen) --- */
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

/* --- Globale GL-tilstande (brugt i svar) --- */
static const char *g_glver = "?";
static const char *g_glrenderer = "?";
static GLuint g_prog = 0;
static GLint g_pos_loc = -1;
static GLint g_phase_loc = -1;

/*
 * Wrapperens _glReadPixels-slot (BSS, offset 0x101dc fundet via disassembly af
 * glReadPixels_wrapper i libGLESv2.so.2) er NULL — init'ens android_dlsym løste
 * symbolet ikke. Her resolver vi den ægte DDK-funktion (libGLESv2_POWERVR_ROGUE.so
 * i /system/vendor/lib/egl) via hybris' android-linker og skriver den ind i
 * slottet. Uden dette: SIGSEGV (NULL-kald) i glReadPixels → vendor-handleren
 * fanger det → refresh_display-dansen + exit(42). Målt med gdb+strace, aug 2026.
 */
static void patch_readpixels(void)
{
    Dl_info info;
    if (!dladdr((void *)&glClear, &info) || !info.dli_fbase) {
        fprintf(stderr, "gles_daemon: dladdr(libGLESv2) fejlede — readback virker ikke\n");
        return;
    }
    void **slot = (void **)((char *)info.dli_fbase + 0x101dc);
    void *h = hybris_dlopen("libGLESv2.so", 0);
    if (!h) {
        fprintf(stderr, "gles_daemon: hybris_dlopen(libGLESv2.so) fejlede\n");
        return;
    }
    void *real = hybris_dlsym(h, "glReadPixels");
    if (!real) {
        fprintf(stderr, "gles_daemon: hybris_dlsym(glReadPixels) fejlede\n");
        return;
    }
    *slot = real;
    printf("gles_daemon: _glReadPixels patchet (%p <- %p)\n", (void *)slot, real);
}

/* ------------------------------------------------------------------ */
/* fb0: geometri + blit                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int fd;
    int xres, yres, xoff, yoff;
    int bpp, bytespp, stride;
    unsigned int r_off, r_len, g_off, g_len, b_off, b_len;
    size_t smem_len;
    unsigned char *map;
} fbdev_t;

static int fb_init(fbdev_t *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) {
        fprintf(stderr, "gles_daemon: kan ikke åbne /dev/fb0: %s\n", strerror(errno));
        return -1;
    }
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &v) != 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &f) != 0) {
        fprintf(stderr, "gles_daemon: FBIOGET fejlede: %s\n", strerror(errno));
        return -1;
    }
    fb->xres = v.xres;
    fb->yres = v.yres;
    fb->xoff = v.xoffset;
    fb->yoff = v.yoffset;
    fb->bpp = v.bits_per_pixel;
    fb->bytespp = fb->bpp / 8;
    fb->stride = f.line_length;
    fb->smem_len = f.smem_len;
    fb->r_off = v.red.offset;   fb->r_len = v.red.length;
    fb->g_off = v.green.offset; fb->g_len = v.green.length;
    fb->b_off = v.blue.offset;  fb->b_len = v.blue.length;
    if (fb->bytespp < 2 || fb->stride <= 0 || fb->smem_len == 0) {
        fprintf(stderr, "gles_daemon: mistænkelig fb0-geometri: %dx%d bpp=%d stride=%d smem=%zu\n",
                fb->xres, fb->yres, fb->bpp, fb->stride, fb->smem_len);
        return -1;
    }
    fb->map = (unsigned char *)mmap(NULL, fb->smem_len, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fb->fd, 0);
    if (fb->map == MAP_FAILED) {
        fprintf(stderr, "gles_daemon: mmap af fb0 fejlede: %s\n", strerror(errno));
        return -1;
    }
    printf("gles_daemon: fb0 = %dx%d bpp=%d stride=%d (%zu bytes) offset=%d,%d\n",
           fb->xres, fb->yres, fb->bpp, fb->stride, fb->smem_len, fb->xoff, fb->yoff);
    return 0;
}

/* top r_len bits af 8-bit kanal, maskeret til kanalens længde */
static inline uint32_t ch(uint32_t v, int len)
{
    if (len <= 0) return 0;
    if (len >= 8) return v;
    return (v >> (8 - len)) & ((1u << len) - 1);
}

static inline uint32_t pack_px(const fbdev_t *fb, int r, int g, int b)
{
    return (ch((uint32_t)r, fb->r_len) << fb->r_off) |
           (ch((uint32_t)g, fb->g_len) << fb->g_off) |
           (ch((uint32_t)b, fb->b_len) << fb->b_off);
}

/*
 * Blit RGBA8 (fra glReadPixels: række 0 = BOTTOM) til fb0 (række 0 = TOP).
 * flip=1 vender derfor lodret. Kun rect'en røres — frontend tegner rundt om.
 */
static int fb_blit_rgba(const fbdev_t *fb, int x, int y, int w, int h,
                        const unsigned char *rgba, int flip)
{
    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > fb->xres || y + h > fb->yres)
        return -1;
    for (int row = 0; row < h; row++) {
        int src_row = flip ? (h - 1 - row) : row;
        const unsigned char *src = rgba + (size_t)src_row * w * 4;
        unsigned char *dst = fb->map +
            (size_t)(y + fb->yoff + row) * fb->stride +
            (size_t)(x + fb->xoff) * fb->bytespp;
        for (int i = 0; i < w; i++) {
            uint32_t p = pack_px(fb, src[0], src[1], src[2]);
            memcpy(dst, &p, (size_t)fb->bytespp);
            src += 4;
            dst += fb->bytespp;
        }
    }
    return 0;
}

static int fb_fill(const fbdev_t *fb, int x, int y, int w, int h, int r, int g, int b)
{
    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > fb->xres || y + h > fb->yres)
        return -1;
    uint32_t p = pack_px(fb, r, g, b);
    for (int row = 0; row < h; row++) {
        unsigned char *dst = fb->map +
            (size_t)(y + fb->yoff + row) * fb->stride +
            (size_t)(x + fb->xoff) * fb->bytespp;
        for (int i = 0; i < w; i++) {
            memcpy(dst, &p, (size_t)fb->bytespp);
            dst += fb->bytespp;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* GLES: init (som test_triangle) + FBO + scener                            */
/* ------------------------------------------------------------------ */

static hwc_composer_device_1_t *g_hwc = NULL;
static hwc_display_contents_1_t *g_dpy = NULL;

static void present_cb(void *data, struct ANativeWindow *w,
                       struct ANativeWindowBuffer *buf)
{
    /* Bliver aldrig kaldt: vi swapper aldrig. Bibeholdt for API-kompatibilitet. */
    (void)data; (void)w; (void)buf;
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

/* EGL + GLES2 + FBO — init som i test_triangle, men uden hwc-præsentation */
static int gl_init(void)
{
    const hw_module_t *hwc_mod = NULL;
    if (hw_get_module(HWC_HARDWARE_MODULE_ID, &hwc_mod) != 0) {
        fprintf(stderr, "gles_daemon: hw_get_module(HWC) fejlede\n");
        return -1;
    }
    if (hwc_open_1(hwc_mod, &g_hwc) != 0) {
        fprintf(stderr, "gles_daemon: hwc_open_1 fejlede\n");
        return -1;
    }
    g_dpy = (hwc_display_contents_1_t *)calloc(1, sizeof(*g_dpy) + sizeof(hwc_layer_1_t));

    struct ANativeWindow *win =
        HWCNativeWindowCreate(W, H, HAL_PIXEL_FORMAT_RGBA_8888, present_cb, NULL);
    if (!win) {
        fprintf(stderr, "gles_daemon: HWCNativeWindowCreate fejlede\n");
        return -1;
    }
    class WindowHack : public HWComposerNativeWindow {
    public:
        WindowHack() : HWComposerNativeWindow(0, 0, 0) {}
        using HWComposerNativeWindow::setUsage;
    };
    WindowHack *obj = (WindowHack *)((char *)win - 4);
    obj->setUsage(GRALLOC_USAGE_HW_FB);

    EGLDisplay display = eglGetDisplay(NULL);
    if (display == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay fejlede\n"); return -1; }
    if (!eglInitialize(display, NULL, NULL)) {
        fprintf(stderr, "eglInitialize fejlede\n");
        return -1;
    }
    EGLint attr[] = { EGL_BUFFER_SIZE, 32, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(display, attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "eglChooseConfig fejlede\n");
        return -1;
    }
    EGLSurface surface = eglCreateWindowSurface(display, cfg, (EGLNativeWindowType)win, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface fejlede\n");
        return -1;
    }
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext fejlede\n");
        return -1;
    }
    if (!eglMakeCurrent(display, surface, surface, ctx)) {
        fprintf(stderr, "eglMakeCurrent fejlede\n");
        return -1;
    }

    g_glver = (const char *)glGetString(GL_VERSION);
    g_glrenderer = (const char *)glGetString(GL_RENDERER);
    printf("gles_daemon: GL_VERSION=%s\n", g_glver ? g_glver : "?");
    printf("gles_daemon: GL_RENDERER=%s\n", g_glrenderer ? g_glrenderer : "?");

    patch_readpixels();

    g_prog = make_program();
    glUseProgram(g_prog);
    g_pos_loc = glGetAttribLocation(g_prog, "position");
    g_phase_loc = glGetUniformLocation(g_prog, "phase");

    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "gles_daemon: FBO ukomplet (0x%x)\n", st);
        return -1;
    }
    printf("gles_daemon: FBO klar (%dx%d), shader klar (pos=%d phase=%d)\n",
           W, H, g_pos_loc, g_phase_loc);
    return 0;
}

/* render cos-mønster-scenen ind i en RGBA-buffer (rw*rh*4, GL-orientering:
   række 0 = bund). buf skal være allokeret af kalderen. */
static void render_scene_rgba(int rx, int ry, int rw, int rh, double phase,
                              unsigned char *buf)
{
    static const GLfloat vertices[] = {
         0.0f,  1.0f, 0.0f,
        -1.0f,  0.0f, 0.0f,
         0.0f, -1.0f, 0.0f,
         1.0f,  0.0f, 0.0f,
         0.0f,  1.0f, 0.0f,
    };
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_prog);
    glUniform1f(g_phase_loc, (float)phase);
    glUniform4f(glGetUniformLocation(g_prog, "offset"), 0.0f, 0.0f, 0.0f, 0.0f);
    glVertexAttribPointer(g_pos_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(g_pos_loc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
    glFinish();
    glReadPixels(rx, ry, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

static int render_triangle(const fbdev_t *fb, int rx, int ry, int rw, int rh,
                           double phase)
{
    unsigned char *buf = (unsigned char *)malloc((size_t)rw * rh * 4);
    if (!buf) return -1;
    render_scene_rgba(rx, ry, rw, rh, phase, buf);
    int rc = fb_blit_rgba(fb, rx, ry, rw, rh, buf, 1);
    free(buf);
    return rc;
}

/* Pak RGBA8 → RGB565 (little-endian R5G6B5, samme layout som X' 16-bit
   visual på boksen). Række 0 i out = række 0 i rgba (GL-orientering). */
static void pack_rgb565(const unsigned char *rgba, int w, int h,
                        unsigned char *out)
{
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        uint16_t v = (uint16_t)(((rgba[0] >> 3) << 11) |
                                ((rgba[1] >> 2) << 5) |
                                (rgba[2] >> 3));
        out[i * 2] = (unsigned char)(v & 0xff);
        out[i * 2 + 1] = (unsigned char)(v >> 8);
        rgba += 4;
    }
}

/* ------------------------------------------------------------------ */
/* Minimal JSON-subset-parser (kun det protokollen bruger)                  */
/* ------------------------------------------------------------------ */

static const char *js_ws(const char *p)
{
    while (p && *p && (unsigned char)*p <= ' ') p++;
    return p;
}

/* finder "key": i JSON-objektet og returnerer pointer til værdiens start */
static const char *js_key(const char *json, const char *key)
{
    size_t kl = strlen(key);
    for (const char *p = json; p && *p; p++) {
        if (*p != '"') continue;
        if (strncmp(p + 1, key, kl) == 0 && p[kl + 1] == '"') {
            const char *v = js_ws(p + kl + 2);
            if (*v == ':') return js_ws(v + 1);
        }
    }
    return NULL;
}

static int js_str(const char *json, const char *key, char *dst, size_t n)
{
    const char *v = js_key(json, key);
    if (!v || *v != '"') return -1;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i + 1 < n) {
        if (*v == '\\' && v[1]) v++;   /* spring over escapet tegn */
        dst[i++] = *v++;
    }
    if (*v != '"') return -1;
    dst[i] = 0;
    return 0;
}

static int js_num(const char *json, const char *key, double *out)
{
    const char *v = js_key(json, key);
    if (!v) return -1;
    char *end = NULL;
    double d = strtod(v, &end);
    if (end == v) return -1;
    *out = d;
    return 0;
}

/* [a,b,c,...] — n heltal */
static int js_ints(const char *json, const char *key, int *out, int n)
{
    const char *v = js_key(json, key);
    if (!v || *v != '[') return -1;
    v = js_ws(v + 1);
    for (int i = 0; i < n; i++) {
        char *end = NULL;
        long l = strtol(v, &end, 10);
        if (end == v) return -1;
        out[i] = (int)l;
        v = js_ws(end);
        if (i + 1 < n) {
            if (*v != ',') return -1;
            v = js_ws(v + 1);
        }
    }
    return (*v == ']') ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Kommando-håndtering                                                     */
/* ------------------------------------------------------------------ */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* returnerer 1 hvis daemonen skal lukke (quit). For "frame" sættes *out_bin /
   *out_bin_len til et malloc'et RGBA-billede, som main-loopet sender efter
   reply-linjen og frigør. */
static int handle_line(const char *line, const fbdev_t *fb, char *reply, size_t rn,
                       unsigned char **out_bin, size_t *out_bin_len)
{
    *out_bin = NULL;
    *out_bin_len = 0;
    char cmd[64];
    if (js_str(line, "cmd", cmd, sizeof cmd) != 0) {
        snprintf(reply, rn, "{\"ok\":false,\"error\":\"manglende eller ugyldig cmd\"}");
        return 0;
    }

    if (strcmp(cmd, "ping") == 0) {
        snprintf(reply, rn,
                 "{\"ok\":true,\"cmd\":\"ping\",\"gl\":\"%s\",\"renderer\":\"%s\"}",
                 g_glver ? g_glver : "?", g_glrenderer ? g_glrenderer : "?");
        return 0;
    }

    if (strcmp(cmd, "fb") == 0) {
        snprintf(reply, rn,
                 "{\"ok\":true,\"cmd\":\"fb\",\"fb\":{\"xres\":%d,\"yres\":%d,"
                 "\"xoffset\":%d,\"yoffset\":%d,\"bpp\":%d,\"stride\":%ld,"
                 "\"bytespp\":%d}}",
                 fb->xres, fb->yres, fb->xoff, fb->yoff, fb->bpp,
                 (long)fb->stride, fb->bytespp);
        return 0;
    }

    if (strcmp(cmd, "scenes") == 0) {
        snprintf(reply, rn, "{\"ok\":true,\"cmd\":\"scenes\",\"scenes\":[\"triangle\"]}");
        return 0;
    }

    if (strcmp(cmd, "render") == 0) {
        int rect[4] = { 0, 0, fb->xres, fb->yres };
        js_ints(line, "rect", rect, 4);   /* default: hele skærmen */
        if (rect[2] <= 0 || rect[3] <= 0 || rect[0] < 0 || rect[1] < 0 ||
            rect[0] + rect[2] > fb->xres || rect[1] + rect[3] > fb->yres) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"render\",\"error\":\"rect uden for skærmen: [%d,%d,%d,%d]\"}",
                     rect[0], rect[1], rect[2], rect[3]);
            return 0;
        }
        char scene[32] = "triangle";
        js_str(line, "scene", scene, sizeof scene);
        if (strcmp(scene, "triangle") != 0) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"render\",\"error\":\"ukendt scene: %s\"}", scene);
            return 0;
        }
        double phase = 0.0;
        js_num(line, "phase", &phase);
        double t0 = now_ms();
        int rc = render_triangle(fb, rect[0], rect[1], rect[2], rect[3], phase);
        double ms = now_ms() - t0;
        if (rc != 0) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"render\",\"error\":\"blit til fb0 fejlede\"}");
            return 0;
        }
        snprintf(reply, rn,
                 "{\"ok\":true,\"cmd\":\"render\",\"scene\":\"triangle\","
                 "\"rect\":[%d,%d,%d,%d],\"phase\":%.3f,\"ms\":%.1f}",
                 rect[0], rect[1], rect[2], rect[3], phase, ms);
        return 0;
    }

    if (strcmp(cmd, "frame") == 0) {
        int rect[4] = { 0, 0, W, H };
        js_ints(line, "rect", rect, 4);   /* default: hele FBO'en */
        if (rect[2] <= 0 || rect[3] <= 0 || rect[0] < 0 || rect[1] < 0 ||
            rect[0] + rect[2] > W || rect[1] + rect[3] > H) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"frame\",\"error\":\"rect uden for FBO'en: [%d,%d,%d,%d]\"}",
                     rect[0], rect[1], rect[2], rect[3]);
            return 0;
        }
        char scene[32] = "triangle";
        js_str(line, "scene", scene, sizeof scene);
        if (strcmp(scene, "triangle") != 0) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"frame\",\"error\":\"ukendt scene: %s\"}", scene);
            return 0;
        }
        double phase = 0.0;
        js_num(line, "phase", &phase);
        char fmt[16] = "rgba8";
        js_str(line, "fmt", fmt, sizeof fmt);
        unsigned char *buf = (unsigned char *)malloc((size_t)rect[2] * rect[3] * 4);
        if (!buf) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"frame\",\"error\":\"malloc fejlede\"}");
            return 0;
        }
        double t0 = now_ms();
        render_scene_rgba(rect[0], rect[1], rect[2], rect[3], phase, buf);
        double ms = now_ms() - t0;
        if (strcmp(fmt, "rgb565") == 0) {
            size_t n = (size_t)rect[2] * rect[3] * 2;
            unsigned char *packed = (unsigned char *)malloc(n);
            if (!packed) {
                free(buf);
                snprintf(reply, rn,
                         "{\"ok\":false,\"cmd\":\"frame\",\"error\":\"malloc fejlede\"}");
                return 0;
            }
            pack_rgb565(buf, rect[2], rect[3], packed);
            free(buf);
            *out_bin = packed;
            *out_bin_len = n;
            snprintf(reply, rn,
                     "{\"ok\":true,\"cmd\":\"frame\",\"w\":%d,\"h\":%d,"
                     "\"fmt\":\"rgb565\",\"bytes\":%zu,\"ms\":%.1f}\n",
                     rect[2], rect[3], n, ms);
        } else {
            *out_bin = buf;
            *out_bin_len = (size_t)rect[2] * rect[3] * 4;
            snprintf(reply, rn,
                     "{\"ok\":true,\"cmd\":\"frame\",\"w\":%d,\"h\":%d,"
                     "\"fmt\":\"rgba8\",\"bytes\":%zu,\"ms\":%.1f}\n",
                     rect[2], rect[3], *out_bin_len, ms);
        }
        return 0;
    }

    if (strcmp(cmd, "clear") == 0) {
        int rect[4] = { 0, 0, fb->xres, fb->yres };
        int color[3] = { 0, 0, 0 };
        js_ints(line, "rect", rect, 4);
        js_ints(line, "color", color, 3);
        if (rect[2] <= 0 || rect[3] <= 0 || rect[0] < 0 || rect[1] < 0 ||
            rect[0] + rect[2] > fb->xres || rect[1] + rect[3] > fb->yres) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"clear\",\"error\":\"rect uden for skærmen: [%d,%d,%d,%d]\"}",
                     rect[0], rect[1], rect[2], rect[3]);
            return 0;
        }
        double t0 = now_ms();
        int rc = fb_fill(fb, rect[0], rect[1], rect[2], rect[3], color[0], color[1], color[2]);
        double ms = now_ms() - t0;
        if (rc != 0) {
            snprintf(reply, rn,
                     "{\"ok\":false,\"cmd\":\"clear\",\"error\":\"fyld af fb0 fejlede\"}");
            return 0;
        }
        snprintf(reply, rn,
                 "{\"ok\":true,\"cmd\":\"clear\",\"rect\":[%d,%d,%d,%d],"
                 "\"color\":[%d,%d,%d],\"ms\":%.1f}",
                 rect[0], rect[1], rect[2], rect[3], color[0], color[1], color[2], ms);
        return 0;
    }

    if (strcmp(cmd, "quit") == 0) {
        snprintf(reply, rn, "{\"ok\":true,\"cmd\":\"quit\"}");
        return 1;
    }

    snprintf(reply, rn, "{\"ok\":false,\"error\":\"ukendt kommando: %s\"}", cmd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                    */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv)
{
    const char *sock_path = argc > 1 ? argv[1] : SOCK_PATH_DEFAULT;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    fbdev_t fb;
    if (fb_init(&fb) != 0)
        return 1;
    if (gl_init() != 0)
        return 1;

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("gles_daemon: socket");
        return 1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof addr.sun_path - 1);
    unlink(sock_path);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(lfd, 4) != 0) {
        perror("gles_daemon: bind/listen");
        return 1;
    }
    chmod(sock_path, 0666);
    printf("gles_daemon: lytter på %s\n", sock_path);
    fflush(stdout);

    while (g_running) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR && !g_running)
                break;
            continue;
        }
        char buf[CMD_MAX];
        size_t n = 0;
        while (g_running) {
            char ch;
            ssize_t r = read(cfd, &ch, 1);
            if (r == 0)
                break;                     /* klient lukkede */
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (ch == '\n') {
                buf[n] = 0;
                char reply[4096];
                unsigned char *bin = NULL;
                size_t bin_len = 0;
                int quit = handle_line(buf, &fb, reply, sizeof reply, &bin, &bin_len);
                printf("gles_daemon: <- %s\n", buf);
                printf("gles_daemon: -> %s\n", reply);
                fflush(stdout);
                ssize_t wr = write(cfd, reply, strlen(reply));
                (void)wr;
                if (bin) {
                    wr = write(cfd, bin, bin_len);
                    (void)wr;
                    free(bin);
                }
                if (quit) {
                    g_running = 0;
                    break;
                }
                n = 0;
            } else if (n + 1 < sizeof buf) {
                buf[n++] = ch;
            }
        }
        close(cfd);
    }

    close(lfd);
    unlink(sock_path);
    printf("gles_daemon: lukker\n");
    return 0;
}
