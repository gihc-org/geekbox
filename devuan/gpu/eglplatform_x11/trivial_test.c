/* trivial_test.c — isolerer driver-kompilerens adfærd på GLSL-extensioner.
 * Målt 26. aug 2026 (PowerVR Rogue G6110, 2016-blob):
 *   - triviel shader:                  OK
 *   - tom shader:                      OK
 *   - "#extension GL_EXT_draw_buffers: require" + tom main: FEJL
 *     ("Extension GL_EXT_draw_buffers not supported")
 * → kompileren har en hardkodet (lukket) liste over GLSL-extensioner;
 *   draw_buffers står ikke på den, selvom GL_EXTENSIONS adverterer den.
 *
 * Byg på boksen:
 *   gcc -o /tmp/trivial_test trivial_test.c -I/usr/local/include \
 *       -L/opt/hybris -Wl,-rpath-link,/opt/hybris -lEGL -lGLESv2 \
 *       -lhybris-common -ldl -lrt -lm
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

static void t(EGLDisplay d, EGLConfig cfg, const char *tag, const char *src)
{
    EGLint attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, attr);
    if (c == EGL_NO_CONTEXT) {
        printf("%s: ctx fail\n", tag);
        return;
    }
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
        printf("%s: mc fail\n", tag);
        return;
    }
    GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[1024] = { 0 };
    glGetShaderInfoLog(s, 1023, NULL, log);
    printf("%s: %s | info=%s\n", tag, ok ? "OK" : "FEJL", log[0] ? log : "(tom)");
}

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, NULL, NULL)) {
        printf("init fail\n");
        return 1;
    }
    EGLint a[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(d, a, &cfg, 1, &n) || n < 1) {
        printf("no cfg\n");
        return 1;
    }
    const char *trivial = "void main(){ gl_FragColor=vec4(1.0); }\n";
    const char *with_ext =
        "#extension GL_EXT_draw_buffers : require\n"
        "void main(){ gl_FragColor=vec4(1.0); }\n";
    const char *no_frag = "void main(){}\n";
    t(d, cfg, "trivial", trivial);
    t(d, cfg, "med-ext", with_ext);
    t(d, cfg, "tom", no_frag);
    return 0;
}
