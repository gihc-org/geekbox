/* gl_version_probe.c — viser GL_VERSION/GL_RENDERER fra den aktive hybris/DDK-
 * stak. Bruges til at verificere "OpenGL ES 3.1 build 1.5@3830101" (eller 1.4)
 * uden at skulle åbne Firefox about:support.
 *
 * Byg på boksen:
 *   gcc -o /tmp/gl_version_probe gl_version_probe.c -I/usr/local/include \
 *       -L/opt/hybris -Wl,-rpath-link,/opt/hybris -lEGL -lGLESv2 \
 *       -lhybris-common -ldl -lrt -lm
 * Kør:
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *     EGL_PLATFORM=x11 DISPLAY=:0 /tmp/gl_version_probe
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, NULL, NULL)) {
        printf("init fail\n");
        return 1;
    }
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) {
        printf("no cfg\n");
        return 1;
    }
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
        printf("ctx fail\n");
        return 1;
    }
    printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
    return 0;
}
