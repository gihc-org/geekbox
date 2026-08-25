// eglplatform_x11.cpp — libhybris EGL-platform: PVR/GLES renderer offscreen ind
// i gralloc-buffere og præsenterer dem i et X-vindue via XPutImage.
//
// Prototype (24. aug 2026) efter BROWSER-VEJE A3-kontrakten (ws.h/ws_module)
// og facitlisten fra eglplatform_hwcomposer/fbdev (nm -D). Bygges på boksen
// (fallback-host):  bash devuan/gpu/eglplatform_x11/build_box.sh
//
// EGL_PLATFORM=x11 → libEGL dlopen'er /usr/local/lib/libhybris/eglplatform_x11.so
// og kalder ws_module_info. Testklient: test_client_x11.cpp.
//
// V1: bufferne er RGBA_8888 fra gralloc (GRALLOC_USAGE_HW_FB — målt: HW_COMPOSER
// giver ENOMEM i PVR-grallocen, se test_triangle), og X-dybden på boksen er 16,
// så vi pakker 8888 → RGB565 før XPutImage. Depth 32 klarer vi ved direkte
// XPutImage. XShm er bevidst udeladt i v1 (B8 droppet; tuning senere).

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <hybris/eglplatformcommon/ws.h>
extern "C" {
#include <hybris/eglplatformcommon/eglplatformcommon.h>
}
#include <hybris/eglplatformcommon/nativewindowbase.h>
#include <android/hardware/gralloc.h>
#include <android/system/graphics.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <dirent.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <dlfcn.h>

#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#endif
#ifndef HAL_PIXEL_FORMAT_RGBX_8888
#define HAL_PIXEL_FORMAT_RGBX_8888 2
#endif
#ifndef HAL_PIXEL_FORMAT_RGB_565
#define HAL_PIXEL_FORMAT_RGB_565 4
#endif

static gralloc_module_t *g_gralloc = NULL;
static alloc_device_t *g_alloc = NULL;
static Display *g_dpy = NULL;
static unsigned long g_present_count = 0;

/* Hybris' EGL-init (machybrisegl) skifter aktiv VT væk fra X' VT (målt: →10),
 * og X' fbdev-driver kopierer KUN shadow→fb0, når X' VT er aktiv — ellers når
 * tegning aldrig skærmen (målt 24. aug 2026: alle tegninger usynlige, indtil
 * frisk chvt 8 genoprettede rendering). Denne helper finder X' VT via
 * /proc/<Xorg-pid>/cmdline og aktiverer den. */
static void ensure_x_vt(void)
{
    /* NB: ingen popen/pgrep — fork/exec fejler i hybris-processer (ødelagt
     * environ, samme fælde som system()); scan /proc direkte. */
    int num = 0;
    DIR *dir = opendir("/proc");
    struct dirent *ent;
    while (dir && (ent = readdir(dir))) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9')
            continue;
        char path[64], cmd[4096] = "";
        snprintf(path, sizeof path, "/proc/%s/cmdline", ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        ssize_t n = read(fd, cmd, sizeof cmd - 1);
        close(fd);
        if (n <= 0)
            continue;
        cmd[n] = 0;
        if (!strstr(cmd, "Xorg"))
            continue;
        /* cmdline har NUL-mellemrum mellem argumenterne — strstr stopper ved
         * første NUL, så søg argument-for-argument */
        const char *vt = NULL;
        for (const char *p = cmd; p < cmd + n; p += strlen(p) + 1) {
            if (strncmp(p, "vt", 2) == 0) {
                vt = p;
                break;
            }
        }
        if (vt) {
            num = atoi(vt + 2);
            fprintf(stderr, "x11ws: ensure_x_vt fandt Xorg (%s), vt=%d\n",
                    ent->d_name, num);
            break;
        }
    }
    if (dir)
        closedir(dir);
    if (num <= 0)
        return;
    int tty = open("/dev/tty0", O_RDWR);
    if (tty < 0) {
        fprintf(stderr, "x11ws: kan ikke åbne /dev/tty0\n");
        return;
    }
    int rc1 = ioctl(tty, VT_ACTIVATE, num);
    int rc2 = ioctl(tty, VT_WAITACTIVE, num);
    close(tty);
    fprintf(stderr, "x11ws: aktiv VT skiftet til X' vt%d (rc=%d,%d)\n",
            num, rc1, rc2);
}

static int x11_error_handler(Display *dpy, XErrorEvent *ev)
{
    char buf[256];
    XGetErrorText(dpy, ev->error_code, buf, sizeof buf);
    fprintf(stderr, "x11ws: X-fejl: %s (request %d, resource 0x%lx)\n",
            buf, ev->request_code, ev->resourceid);
    return 0;
}

class X11NativeWindowBuffer : public BaseNativeWindowBuffer {
public:
    X11NativeWindowBuffer(alloc_device_t *alloc, unsigned int w, unsigned int h,
                          unsigned int fmt, unsigned int usage)
        : m_alloc(alloc), busy(0)
    {
        int out_stride = 0;
        if (m_alloc && m_alloc->alloc(m_alloc, (int)w, (int)h, (int)fmt, (int)usage,
                                      &handle, &out_stride) != 0) {
            fprintf(stderr, "x11ws: gralloc alloc fejlede (%ux%u fmt=%u usage=%x)\n",
                    w, h, fmt, usage);
            handle = NULL;
            out_stride = 0;
        }
        width = (int)w;
        height = (int)h;
        format = (int)fmt;
        this->usage = (int)usage;
        stride = out_stride;
        /* Målt i vendors BaseNativeWindowBuffer-konstruktør: den sætter KUN
         * decRef-slotten ([28]) og efterlader incRef ([24]) = NULL — men PVR's
         * WSEGL kalder incRef på bufferen ved swap (SIGSEGV 0x0 i
         * libpvrANDROID_WSEGL). Vi overskriver begge med no-ops: poolen ejes
         * af vinduet (busy-flag + destroyBuffers), så refcount er uden
         * betydning for levetiden i v1. */
        common.incRef = x11buf_incRef;
        common.decRef = x11buf_decRef;
    }
    virtual ~X11NativeWindowBuffer()
    {
        if (handle && m_alloc)
            m_alloc->free(m_alloc, handle);
    }

    alloc_device_t *m_alloc;
    int busy;

private:
    static void x11buf_incRef(struct android_native_base_t *base) { (void)base; }
    static void x11buf_decRef(struct android_native_base_t *base) { (void)base; }
};

class X11NativeWindow : public BaseNativeWindow {
public:
    X11NativeWindow(Display *dpy, Window win, unsigned int w, unsigned int h)
        : m_dpy(dpy), m_win(win), m_width(w), m_height(h),
          m_format(HAL_PIXEL_FORMAT_RGBA_8888),
          m_usage(GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_SW_READ_OFTEN),
          m_bufferCount(0), m_nextBuffer(0), m_interval(1), m_sizeDirty(false)
    {
    }
    virtual ~X11NativeWindow()
    {
        destroyBuffers();
    }

    /* Hent vinduets LEVENDE X-størrelse. Firefox opretter kompositorvinduet
     * som 1x1 og resizer det bagefter; EGL'en spørger kun ved
     * eglCreateWindowSurface → uden dette fryser overfladen på 1x1
     * (målt 25. aug 2026: x11ws "vindue pakket ind (1x1)" mens xwininfo
     * viser 1280x948; browseren venter på compositorens første frame). */
    void refresh_size() const
    {
        XWindowAttributes a;
        if (!m_dpy || !XGetWindowAttributes(m_dpy, m_win, &a))
            return;
        unsigned int w = (unsigned int)a.width;
        unsigned int h = (unsigned int)a.height;
        if (w != m_width || h != m_height) {
            fprintf(stderr, "x11ws: vindue 0x%lx ændret størrelse -> %ux%u\n",
                    (unsigned long)m_win, w, h);
            m_width = w;
            m_height = h;
            m_sizeDirty = true; /* reallokér ved næste dequeueBuffer */
        }
    }

    int set_interval(int interval) { return setSwapInterval(interval); }

protected:
    virtual int setSwapInterval(int interval)
    {
        m_interval = interval;
        return NO_ERROR;
    }

    virtual int dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd)
    {
        if (m_sizeDirty) {
            m_sizeDirty = false;
            destroyBuffers();
        }
        if (m_bufList.empty())
            allocateBuffers(2);
        for (unsigned int i = 0; i < m_bufList.size(); i++) {
            X11NativeWindowBuffer *b =
                m_bufList[(m_nextBuffer + i) % m_bufList.size()];
            if (!b->busy) {
                b->busy = 1;
                m_nextBuffer = (m_nextBuffer + i + 1) % m_bufList.size();
                *buffer = b;
                if (fenceFd)
                    *fenceFd = -1;
                return NO_ERROR;
            }
        }
        fprintf(stderr, "x11ws: alle buffere er busy\n");
        return BAD_VALUE;
    }

    virtual int queueBuffer(BaseNativeWindowBuffer *buffer, int fenceFd)
    {
        (void)fenceFd;
        X11NativeWindowBuffer *b = static_cast<X11NativeWindowBuffer *>(buffer);
        refresh_size();
        present(b);
        b->busy = 0;
        return NO_ERROR;
    }

    virtual int cancelBuffer(BaseNativeWindowBuffer *buffer, int fenceFd)
    {
        (void)fenceFd;
        static_cast<X11NativeWindowBuffer *>(buffer)->busy = 0;
        return NO_ERROR;
    }

    virtual int lockBuffer(BaseNativeWindowBuffer *buffer)
    {
        (void)buffer; /* deprecated no-op i EGL-stien */
        return NO_ERROR;
    }

    virtual unsigned int type() const { return NATIVE_WINDOW_SURFACE; }
    virtual unsigned int width() const {
        refresh_size();
        return m_width;
    }
    virtual unsigned int height() const {
        refresh_size();
        return m_height;
    }
    virtual unsigned int format() const { return m_format; }
    virtual unsigned int defaultWidth() const {
        refresh_size();
        return m_width;
    }
    virtual unsigned int defaultHeight() const {
        refresh_size();
        return m_height;
    }
    virtual unsigned int queueLength() const { return m_bufList.size(); }
    virtual unsigned int transformHint() const { return 0; }

    virtual int setUsage(int usage)
    {
        m_usage = (unsigned int)usage | GRALLOC_USAGE_SW_READ_OFTEN;
        return NO_ERROR;
    }
    virtual int setBuffersFormat(int format)
    {
        if (m_format != (unsigned int)format) {
            m_format = (unsigned int)format;
            destroyBuffers();
        }
        return NO_ERROR;
    }
    virtual int setBuffersDimensions(int width, int height)
    {
        if (m_width != (unsigned int)width || m_height != (unsigned int)height) {
            m_width = (unsigned int)width;
            m_height = (unsigned int)height;
            destroyBuffers();
        }
        return NO_ERROR;
    }
    virtual int setBufferCount(int cnt)
    {
        if (m_bufferCount != (unsigned int)cnt) {
            m_bufferCount = (unsigned int)cnt;
            destroyBuffers();
        }
        return NO_ERROR;
    }

private:
    void allocateBuffers(unsigned int n)
    {
        for (unsigned int i = 0; i < n; i++) {
            X11NativeWindowBuffer *b = new X11NativeWindowBuffer(
                g_alloc, m_width, m_height, m_format, m_usage);
            if (!b->handle) {
                delete b;
                continue;
            }
            m_bufList.push_back(b);
        }
        m_bufferCount = (unsigned int)m_bufList.size();
        fprintf(stderr, "x11ws: %u buffer(e) allokeret (%ux%u fmt=%u usage=%x)\n",
                m_bufferCount, m_width, m_height, m_format, m_usage);
    }

    void destroyBuffers()
    {
        for (size_t i = 0; i < m_bufList.size(); i++)
            delete m_bufList[i];
        m_bufList.clear();
        m_bufferCount = 0;
    }

    void present(X11NativeWindowBuffer *b)
    {
        void *ptr = NULL;
        g_present_count++;
        if (g_present_count == 1)
            ensure_x_vt(); /* EGL-init har skiftet VT væk fra X (målt fælde) */
        if (!g_gralloc) {
            fprintf(stderr, "x11ws: ingen gralloc\n");
            return;
        }
        int rc = g_gralloc->lock(g_gralloc, b->handle,
                                 GRALLOC_USAGE_SW_READ_OFTEN,
                                 0, 0, b->width, b->height, &ptr);
        if (rc != 0 || !ptr) {
            fprintf(stderr, "x11ws: gralloc lock fejlede (rc=%d)\n", rc);
            return;
        }
        if (g_present_count <= 2 || g_present_count % 50 == 0) {
            const unsigned char *p = (const unsigned char *)ptr;
            fprintf(stderr,
                    "x11ws: present #%lu (%dx%d fmt=%u stride=%d) pix0=%02x%02x%02x%02x "
                    "pix1=%02x%02x%02x%02x\n",
                    g_present_count, b->width, b->height, b->format, b->stride,
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        }
        put_image((const unsigned char *)ptr, b->width, b->height,
                  b->stride, b->format);
        g_gralloc->unlock(g_gralloc, b->handle);
    }

    void put_image(const unsigned char *src, int w, int h, int stride, int fmt)
    {
        int scr = DefaultScreen(m_dpy);
        int depth = DefaultDepth(m_dpy, scr);
        if (depth == 16) {
            std::vector<unsigned char> rgb565((size_t)w * h * 2);
            for (int y = 0; y < h; y++) {
                const unsigned char *row = src + (size_t)y * stride * 4;
                unsigned char *dst = &rgb565[(size_t)y * w * 2];
                for (int x = 0; x < w; x++) {
                    unsigned int r = row[x * 4 + 0];
                    unsigned int g = row[x * 4 + 1];
                    unsigned int b = row[x * 4 + 2];
                    unsigned int v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    dst[x * 2] = (unsigned char)(v & 0xff);
                    dst[x * 2 + 1] = (unsigned char)(v >> 8);
                }
            }
            put_ximage(&rgb565[0], w, h, 16, w * 2);
        } else if (depth == 32) {
            put_ximage(src, w, h, 32, stride * 4);
        } else {
            fprintf(stderr, "x11ws: X-depth %d endnu ikke understøttet (16/32)\n", depth);
        }
    }

    void put_ximage(const unsigned char *data, int w, int h, int depth, int bpl)
    {
        int scr = DefaultScreen(m_dpy);
        Visual *vis = DefaultVisual(m_dpy, scr);
        XImage *img = XCreateImage(m_dpy, vis, depth, ZPixmap, 0,
                                   (char *)data, w, h,
                                   depth == 16 ? 16 : 32, bpl);
        if (!img) {
            fprintf(stderr, "x11ws: XCreateImage fejlede\n");
            return;
        }
        XPutImage(m_dpy, m_win, DefaultGC(m_dpy, DefaultScreen(m_dpy)),
                  img, 0, 0, 0, 0, w, h);
        XFlush(m_dpy);
        XSync(m_dpy, False); /* tving protokol-fejl frem nu (logges af handler) */
        img->data = NULL; /* data ejes af kalderen — lad XDestroyImage ikke frigøre */
        XDestroyImage(img);
    }

    Display *m_dpy;
    Window m_win;
    mutable unsigned int m_width;
    mutable unsigned int m_height;
    unsigned int m_format;
    unsigned int m_usage;
    unsigned int m_bufferCount;
    unsigned int m_nextBuffer;
    int m_interval;
    mutable bool m_sizeDirty;
    std::vector<X11NativeWindowBuffer *> m_bufList;
};

/* ------------------------------------------------------------------ */
/* ws_module-eksport (facitlisten fra A3: hwcomposer/fbdev-mønstret)  */
/* ------------------------------------------------------------------ */

extern "C" {

static struct _EGLDisplay *x11ws_GetDisplay(EGLNativeDisplayType native)
{
    /* Klienten sender sit Display* som EGL-native-display (eglGetDisplay(dpy)).
     * Målt (24. aug 2026): tegning fra platformens EGEN X-forbindelse når IKKE
     * skærmen på denne fbdev-server (shadow-framebuffer/quirk) — kun vinduets
     * egen forbindelse renderer. Derfor tegner present() via klientens dpy. */
    if (native) {
        g_dpy = (Display *)native;
        fprintf(stderr, "x11ws: GetDisplay modtog klientens X-forbindelse %p\n",
                (void *)g_dpy);
    }
    static struct _EGLDisplay dpy = { EGL_DEFAULT_DISPLAY };
    return &dpy;
}

static void x11ws_Terminate(struct _EGLDisplay *display)
{
    (void)display;
}

static EGLNativeWindowType x11ws_CreateWindow(EGLNativeWindowType win,
                                              struct _EGLDisplay *display)
{
    (void)display;
    if (!g_dpy) {
        fprintf(stderr, "x11ws: intet X-display (init_module ikke kaldt?)\n");
        return 0;
    }
    Window xid = (Window)(uintptr_t)win;
    XWindowAttributes a;
    if (!XGetWindowAttributes(g_dpy, xid, &a)) {
        fprintf(stderr, "x11ws: XGetWindowAttributes fejlede for 0x%lx\n",
                (unsigned long)xid);
        return 0;
    }
    /* Firefox opretter kompositorvinduet som 1x1 og resizer det bagefter;
     * EGL'en spørger kun størrelsen ÉN gang (ved surface-creation) — vent
     * derfor kort på den reelle størrelse (målt 25. aug 2026: uden dette
     * fryser EGL-overfladen på 1x1 og compositoren når aldrig første frame). */
    for (int i = 0; i < 50 && (int)a.width <= 1 && (int)a.height <= 1; i++) {
        usleep(40000);
        if (!XGetWindowAttributes(g_dpy, xid, &a))
            break;
    }
    X11NativeWindow *nw = new X11NativeWindow(g_dpy, xid,
                                              (unsigned int)a.width,
                                              (unsigned int)a.height);
    fprintf(stderr, "x11ws: vindue 0x%lx pakket ind (%dx%d)\n",
            (unsigned long)xid, a.width, a.height);
    return *nw;
}

static void x11ws_DestroyWindow(EGLNativeWindowType win)
{
    ANativeWindow *aw = (ANativeWindow *)win;
    delete static_cast<X11NativeWindow *>(aw);
}

static void *x11ws_wrapper_symbol(const char *name)
{
    /* Returnér wrapperens egen eksport af `name` (fx eglGetDisplay). Bruges
     * af ws_eglGetProcAddress: Firefox' glxtest henter KERNE-EGL-funktioner
     * gennem eglGetProcAddress — falder de igennem til Android-loaderen,
     * får vi dens INTERNE funktioner (eglGetDisplay afviser non-default
     * displays med 300C og omgår X11-platformen helt; målt 24. aug 2026). */
    static void *wrapper_handle = NULL;
    if (!wrapper_handle) {
        wrapper_handle = dlopen("/opt/hybris/libEGL.so.1",
                                RTLD_NOW | RTLD_NOLOAD);
        if (!wrapper_handle)
            wrapper_handle = dlopen("/opt/hybris/libEGL.so.1", RTLD_NOW);
    }
    return wrapper_handle ? dlsym(wrapper_handle, name) : NULL;
}

/* EGL_EXT_device_base: glxtest kræver funktionerne non-NULL (og kalder
 * eglQueryDisplayAttribEXT direkte), men vi har ikke noget EGLDevice at
 * melde — returnér tomt/falsk, så Firefox' probe hopper pænt forbi. */
static const char *x11ws_eglQueryDeviceStringEXT(void *device, EGLint name)
{
    (void)device;
    (void)name;
    return NULL;
}

static EGLBoolean x11ws_eglQueryDisplayAttribEXT(EGLDisplay dpy, EGLint name,
                                                 intptr_t *value)
{
    (void)dpy;
    (void)name;
    (void)value;
    return EGL_FALSE;
}

static __eglMustCastToProperFunctionPointerType
x11ws_eglGetProcAddress(const char *procname)
{
    if (strcmp(procname, "eglQueryDeviceStringEXT") == 0)
        return (__eglMustCastToProperFunctionPointerType)
            x11ws_eglQueryDeviceStringEXT;
    if (strcmp(procname, "eglQueryDisplayAttribEXT") == 0)
        return (__eglMustCastToProperFunctionPointerType)
            x11ws_eglQueryDisplayAttribEXT;
    if (strcmp(procname, "eglGetProcAddress") != 0) {
        void *wrapper_fn = x11ws_wrapper_symbol(procname);
        if (wrapper_fn)
            return (__eglMustCastToProperFunctionPointerType)wrapper_fn;
    }
    return eglplatformcommon_eglGetProcAddress(procname);
}

static void x11ws_passthroughImageKHR(EGLContext *ctx, EGLenum *target,
                                      EGLClientBuffer *buffer,
                                      const EGLint **attrib_list)
{
    eglplatformcommon_passthroughImageKHR(ctx, target, buffer, attrib_list);
}

static const char *x11ws_eglQueryString(EGLDisplay dpy, EGLint name,
                                        const char *(*real_eglQueryString)(
                                            EGLDisplay dpy, EGLint name))
{
    if (name == EGL_EXTENSIONS && dpy == EGL_NO_DISPLAY) {
        /* Client-extensioner. Målt (24. aug 2026): Firefox/glxtest kræver
         * EGL_EXT_platform_base — uden den melder proben "libEGL no display"
         * og falder tilbage til Mesa-software. */
        static const char client_exts[] =
            "EGL_KHR_get_all_proc_addresses "
            "EGL_EXT_platform_base EGL_EXT_platform_x11 EGL_KHR_platform_x11";
        return client_exts;
    }
    return real_eglQueryString(dpy, name);
}

static void x11ws_prepareSwap(EGLDisplay dpy, EGLNativeWindowType win,
                              EGLint *damage_rects, EGLint damage_n_rects)
{
    (void)dpy; (void)win; (void)damage_rects; (void)damage_n_rects;
}

static void x11ws_finishSwap(EGLDisplay dpy, EGLNativeWindowType win)
{
    (void)dpy; (void)win;
}

static void x11ws_setSwapInterval(EGLDisplay dpy, EGLNativeWindowType win,
                                  EGLint interval)
{
    (void)dpy;
    ANativeWindow *aw = (ANativeWindow *)win;
    static_cast<X11NativeWindow *>(aw)->set_interval((int)interval);
}

static void x11ws_init_module(struct ws_egl_interface *egl_iface)
{
    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy)
        fprintf(stderr, "x11ws: XOpenDisplay(NULL) fejlede\n");
    if (g_dpy)
        XSetErrorHandler(x11_error_handler);
    /* eglplatformcommon_init GEMER bare gralloc/alloc i sin egen globale
     * struct (målt i disassemblingen: str r0,[r3,#8]; stmia r3,{r1,r2}) —
     * platformen skal selv hente modulet, som vendors hwcomposer-platform
     * gør (hw_get_module + methods->open i hwcomposerws_init_module). */
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                      (const struct hw_module_t **)&g_gralloc) != 0) {
        fprintf(stderr, "x11ws: hw_get_module(gralloc) fejlede\n");
        g_gralloc = NULL;
    }
    if (g_gralloc && gralloc_open(&g_gralloc->common, &g_alloc) != 0) {
        fprintf(stderr, "x11ws: gralloc_open fejlede\n");
        g_alloc = NULL;
    }
    eglplatformcommon_init(egl_iface, g_gralloc, g_alloc);
    fprintf(stderr, "x11ws: init_module færdig (gralloc=%p alloc=%p dpy=%p)\n",
            (void *)g_gralloc, (void *)g_alloc, (void *)g_dpy);
}

struct ws_module ws_module_info = {
    x11ws_init_module,
    x11ws_GetDisplay,
    x11ws_Terminate,
    x11ws_CreateWindow,
    x11ws_DestroyWindow,
    x11ws_eglGetProcAddress,
    x11ws_passthroughImageKHR,
    x11ws_eglQueryString,
    x11ws_prepareSwap,
    x11ws_finishSwap,
    x11ws_setSwapInterval,
};

} /* extern "C" */
