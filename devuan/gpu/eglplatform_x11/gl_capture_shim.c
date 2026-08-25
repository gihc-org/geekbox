// gl_capture_shim.c — LD_PRELOAD-fangstshim til GPU-processens
// WR_POST_UPDATE-reset-detektion (Firefox ESR 140, PowerVR/hybris-stakken).
//
// Baggrund (målt fra ESR 140-kilden, 26. aug 2026):
//   RenderThread::UpdateAndRender() kalder efter hver frame
//   renderer->CheckGraphicsResetStatus(WR_POST_UPDATE, false) →
//   RenderCompositor::IsContextLost(false):
//     if (!glc || (!aForce && !glc->IsSupported(GLFeature::robustness)))
//         return OK;
//     auto resetStatus = glc->fGetGraphicsResetStatus();
//       → GLContext::fGetGraphicsResetStatus():
//           if (mSymbols.fGetGraphicsResetStatus) ... ring rigtige GL
//           else if (!MakeCurrent(true)) return LOCAL_GL_UNKNOWN_CONTEXT_RESET;
//       → UNKNOWN_CONTEXT_RESET (0x8255) → DeviceResetReason::UNKNOWN →
//         HandleDeviceReset(WR_POST_UPDATE) → GPU-proces-genstart.
//
// Vendor-GL mangler glGetGraphicsResetStatus (eglGetProcAddress → NULL, målt
// i gl_reset_probe), så Firefox' mSymbols.fGetGraphicsResetStatus er NULL, og
// UNKNOWN opstår når MakeCurrent(true) fejler. Men hvad der præcist sker på
// boksen er ikke målt — derfor denne shim:
//   - eksporterer glGetGraphicsResetStatus, så PR_FindFunctionSymbol på
//     libGL/libGLESv2-handlen finder OS's version (dlsym → LD_PRELOAD vinder)
//   - wrapper eglMakeCurrent/eglGetError/glGetError/eglSwapBuffers/
//     eglGetProcAddress + glFenceSync/glClientWaitSync/glCompileShader
//   - logger alle kald + returværdier med monotont ur og tråd-id
//
// Byg på boksen:
//   gcc -O2 -fPIC -shared -o /tmp/gl_capture_shim.so \
//       /tmp/gl_capture_shim.c -I/usr/local/include -ldl -lpthread
//
// Kør (før system_shim i LD_PRELOAD, så vores symboler vinder):
//   LD_PRELOAD="/tmp/gl_capture_shim.so /usr/local/lib/firefox-webgl/system_shim.so \
//     /usr/local/lib/firefox-webgl/egl_platform_shim.so" ...
//   GL_CAPTURE_LOG=/tmp/gl_capture.log
//
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* GLES2-headeren på boksen definerer ikke GLsync/GLuint64 (ES 3-typer) */
typedef struct __GLsync *GLsync;
typedef uint64_t GLuint64;

/* ---- logning ---- */
static int g_log_fd = -1;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_open(void)
{
    const char *path = getenv("GL_CAPTURE_LOG");
    if (!path || !*path)
        path = "/tmp/gl_capture.log";
    g_log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

static void log_msg(const char *fmt, ...)
{
    if (g_log_fd < 0)
        return;
    char buf[512];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(buf, sizeof(buf), "%lld.%03lld pid=%d tid=%d ",
                     (long long)ts.tv_sec, (long long)(ts.tv_nsec / 1000000),
                     (int)getpid(), (int)(long)syscall(SYS_gettid));
    if (n < 0 || n >= (int)sizeof(buf))
        n = 0;
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (m < 0)
        return;
    if (n + m >= (int)sizeof(buf)) {
        m = (int)sizeof(buf) - n - 2;
        buf[n + m] = '\n';
        m += 1;
    }
    pthread_mutex_lock(&g_log_lock);
    (void)write(g_log_fd, buf, n + m);
    pthread_mutex_unlock(&g_log_lock);
}

static void log_hex(const char *tag, unsigned int v)
{
    log_msg("%s=0x%04x\n", tag, v);
}

/* ---- konstruktor: åbn logfilen, inden noget kaldes ---- */
__attribute__((constructor)) static void gl_capture_init(void)
{
    log_open();
    log_msg("gl_capture_shim loaded (v1)\n");
}

/* ---- eglGetProcAddress-wrapper: log hvad Firefox slår op ---- */
typedef __eglMustCastToProperFunctionPointerType (*eglGetProcAddress_t)(const char *);
static eglGetProcAddress_t real_eglGetProcAddress;

static void *resolve_gl(const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (p)
        return p;
    /* Vendor-GL eksporterer kun få symboler direkte; resten kommer fra
     * eglGetProcAddress. brug den som fallback. */
    if (!real_eglGetProcAddress)
        real_eglGetProcAddress =
            (eglGetProcAddress_t)dlsym(RTLD_NEXT, "eglGetProcAddress");
    return real_eglGetProcAddress
               ? (void *)real_eglGetProcAddress(name)
               : NULL;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    if (!real_eglGetProcAddress)
        real_eglGetProcAddress =
            (eglGetProcAddress_t)dlsym(RTLD_NEXT, "eglGetProcAddress");
    __eglMustCastToProperFunctionPointerType p =
        real_eglGetProcAddress ? real_eglGetProcAddress(name) : NULL;
    if (name && strstr(name, "GetGraphicsResetStatus"))
        log_msg("eglGetProcAddress(%s) -> %p\n", name, (void *)p);
    return p;
}

/* ---- glGetGraphicsResetStatus: hvis Firefox løser OS's version, ser vi
 *      hvad der returneres (og om den overhovedet bliver kaldt). ---- */
typedef GLenum (*glGetGraphicsResetStatus_t)(void);
static glGetGraphicsResetStatus_t real_glGetGraphicsResetStatus;

GLenum glGetGraphicsResetStatus(void)
{
    if (!real_glGetGraphicsResetStatus)
        real_glGetGraphicsResetStatus =
            (glGetGraphicsResetStatus_t)resolve_gl(
                "glGetGraphicsResetStatus");
    GLenum s = real_glGetGraphicsResetStatus
                   ? real_glGetGraphicsResetStatus()
                   : 0x8255 /* GL_UNKNOWN_CONTEXT_RESET_ARB */;
    log_hex("glGetGraphicsResetStatus ->", s);
    return s;
}

/* ---- glGetError: log kun afvigelser (ikke NO_ERROR-strømmen) ---- */
typedef GLenum (*glGetError_t)(void);
static glGetError_t real_glGetError;

GLenum glGetError(void)
{
    if (!real_glGetError)
        real_glGetError = (glGetError_t)resolve_gl("glGetError");
    GLenum e = real_glGetError ? real_glGetError() : GL_NO_ERROR;
    if (e != GL_NO_ERROR)
        log_hex("glGetError ->", e);
    return e;
}

/* ---- eglGetError: log kun afvigelser ---- */
typedef EGLint (*eglGetError_t)(void);
static eglGetError_t real_eglGetError;

EGLint eglGetError(void)
{
    if (!real_eglGetError)
        real_eglGetError = (eglGetError_t)dlsym(RTLD_NEXT, "eglGetError");
    EGLint e = real_eglGetError ? real_eglGetError() : EGL_SUCCESS;
    if (e != EGL_SUCCESS)
        log_hex("eglGetError ->", (unsigned int)e);
    return e;
}

/* ---- eglMakeCurrent: fang evt. MakeCurrent(true)-fejl i
 *      fGetGraphicsResetStatus()-fallbacken ---- */
typedef EGLBoolean (*eglMakeCurrent_t)(EGLDisplay, EGLSurface, EGLSurface,
                                       EGLContext);
static eglMakeCurrent_t real_eglMakeCurrent;

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx)
{
    if (!real_eglMakeCurrent)
        real_eglMakeCurrent =
            (eglMakeCurrent_t)dlsym(RTLD_NEXT, "eglMakeCurrent");
    EGLBoolean ok = real_eglMakeCurrent
                        ? real_eglMakeCurrent(dpy, draw, read, ctx)
                        : EGL_FALSE;
    if (!ok) {
        EGLint e = real_eglGetError ? real_eglGetError() : EGL_SUCCESS;
        log_msg("eglMakeCurrent FAILED (dpy=%p draw=%p read=%p ctx=%p) "
                "eglGetError=0x%04x\n", (void *)dpy, (void *)draw,
                (void *)read, (void *)ctx, (unsigned int)e);
    }
    return ok;
}

/* ---- eglSwapBuffers: present-tæller + fejl ---- */
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t real_eglSwapBuffers;
static unsigned long g_swap_count;

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    if (!real_eglSwapBuffers)
        real_eglSwapBuffers =
            (eglSwapBuffers_t)dlsym(RTLD_NEXT, "eglSwapBuffers");
    EGLBoolean ok = real_eglSwapBuffers
                        ? real_eglSwapBuffers(dpy, surface)
                        : EGL_FALSE;
    g_swap_count++;
    if (!ok) {
        EGLint e = real_eglGetError ? real_eglGetError() : EGL_SUCCESS;
        log_msg("eglSwapBuffers #%lu FAILED eglGetError=0x%04x\n",
                g_swap_count, (unsigned int)e);
    } else if ((g_swap_count % 50) == 0) {
        log_msg("eglSwapBuffers #%lu ok\n", g_swap_count);
    }
    return ok;
}

/* ---- glFenceSync/glClientWaitSync: PowerVR-fence-stien (bug 1773128) ---- */
typedef GLsync (*glFenceSync_t)(GLenum, GLbitfield);
typedef GLenum (*glClientWaitSync_t)(GLsync, GLbitfield, GLuint64);
static glFenceSync_t real_glFenceSync;
static glClientWaitSync_t real_glClientWaitSync;

GLsync glFenceSync(GLenum condition, GLbitfield flags)
{
    if (!real_glFenceSync)
        real_glFenceSync = (glFenceSync_t)resolve_gl("glFenceSync");
    GLsync s = real_glFenceSync ? real_glFenceSync(condition, flags) : NULL;
    log_msg("glFenceSync(cond=0x%04x flags=0x%x) -> %p\n",
            (unsigned int)condition, (unsigned int)flags, (void *)s);
    return s;
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
    if (!real_glClientWaitSync)
        real_glClientWaitSync =
            (glClientWaitSync_t)resolve_gl("glClientWaitSync");
    GLenum r = real_glClientWaitSync
                   ? real_glClientWaitSync(sync, flags, timeout)
                   : 0;
    if (r != 0x911d /* GL_ALREADY_SIGNALED */)
        log_hex("glClientWaitSync ->", r);
    return r;
}

/* ---- glCompileShader: se shader-kompileringer i GPU-processen ---- */
typedef void (*glCompileShader_t)(GLuint);
static glCompileShader_t real_glCompileShader;

void glCompileShader(GLuint shader)
{
    if (!real_glCompileShader)
        real_glCompileShader =
            (glCompileShader_t)resolve_gl("glCompileShader");
    if (real_glCompileShader)
        real_glCompileShader(shader);
    log_msg("glCompileShader(shader=%u) done\n", shader);
}
