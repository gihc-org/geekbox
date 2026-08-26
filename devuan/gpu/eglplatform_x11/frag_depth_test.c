/* frag_depth_test.c — tester om driver-kompileren accepterer
 * `#extension GL_EXT_frag_depth : require` + gl_FragDepthEXT.
 * Subway Surfers' Unity-shaders kræver BÅDE GL_EXT_draw_buffers OG
 * GL_EXT_frag_depth (målt 26. aug 2026 på 1.4; 1.5 har draw_buffers — denne
 * test afgør om frag_depth også er der).
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

static void compile_tag(EGLDisplay d, EGLConfig cfg, const char *tag, int es3,
                        const char *src)
{
    EGLint attr[] = { EGL_CONTEXT_CLIENT_VERSION, es3 ? 3 : 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, attr);
    if (c == EGL_NO_CONTEXT) { printf("%s: ctx fail\n", tag); return; }
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
        printf("%s: mc fail\n", tag); return;
    }
    GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[512] = { 0 };
    glGetShaderInfoLog(s, 511, NULL, log);
    printf("%s: compile=%s info=%s\n", tag, ok ? "OK" : "FEJL", log);
    glDeleteShader(s);
    eglDestroyContext(d, c);
}

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, NULL, NULL)) { printf("init fail\n"); return 1; }
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n;
    if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) {
        printf("no cfg\n"); return 1;
    }
    compile_tag(d, cfg, "ES2 EXT-frag_depth+gl_FragDepthEXT", 0,
        "#extension GL_EXT_frag_depth : require\n"
        "void main(){ gl_FragColor=vec4(1.0); gl_FragDepthEXT=0.5; }\n");
    compile_tag(d, cfg, "ES3 EXT-frag_depth+gl_FragDepthEXT", 1,
        "#extension GL_EXT_frag_depth : require\n"
        "void main(){ gl_FragColor=vec4(1.0); gl_FragDepthEXT=0.5; }\n");
    compile_tag(d, cfg, "ES3 core gl_FragDepth (ingen EXT)", 1,
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 c;\n"
        "void main(){ c=vec4(1.0); gl_FragDepth=0.5; }\n");
    compile_tag(d, cfg, "ES2 gl_FragDepthEXT UDEN direktiv", 0,
        "void main(){ gl_FragColor=vec4(1.0); gl_FragDepthEXT=0.5; }\n");
    compile_tag(d, cfg, "ES2 gl_FragDepth (core-navn i ES2)", 0,
        "void main(){ gl_FragColor=vec4(1.0); gl_FragDepth=0.5; }\n");
    return 0;
}
