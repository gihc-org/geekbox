/* vertex_tex_test.c — tester om driveren kan kompilere en #version 300 es
 * VERTEX-shader med sampler2D (vertex-texture-støtte). WebRender's cs_blur
 * sampler i vertex-shaderen og fejler med "Compile failed." (26. aug 2026). */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>

static void t(EGLDisplay d, EGLConfig cfg, const char *tag, const char *src, int es3)
{
    EGLint attr[] = { EGL_CONTEXT_CLIENT_VERSION, es3 ? 3 : 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, attr);
    if (c == EGL_NO_CONTEXT) { printf("%s: ctx fail\n", tag); return; }
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
    GLuint s = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[512] = { 0 };
    glGetShaderInfoLog(s, 511, NULL, log);
    printf("%s: compile=%s info=%s\n", tag, ok ? "OK" : "FEJL", log);
    eglDestroyContext(d, c);
}

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, NULL, NULL);
    EGLint a[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n;
    eglChooseConfig(d, a, &cfg, 1, &n);

    t(d, cfg, "ES3 vertex med sampler2D", 
      "#version 300 es\nuniform sampler2D uTex;\nin vec2 aP;\n"
      "void main(){ gl_Position=vec4(aP, texture(uTex, aP).x, 1.0); }\n", 1);
    t(d, cfg, "ES3 vertex UDEN sampler", 
      "#version 300 es\nin vec2 aP;\n"
      "void main(){ gl_Position=vec4(aP, 0.0, 1.0); }\n", 1);
    t(d, cfg, "ES3 fragment med sampler2D", 
      "#version 300 es\nprecision mediump float;\nuniform sampler2D uTex;\n"
      "out vec4 c;\nvoid main(){ c=texture(uTex, vec2(0.5)); }\n", 1);
    t(d, cfg, "ES3 fragment texelFetchOffset", 
      "#version 300 es\nprecision mediump float;\nuniform sampler2D uTex;\n"
      "out vec4 c;\nvoid main(){ c=texelFetchOffset(uTex, ivec2(0,0), 0, ivec2(1,0)); }\n", 1);
    t(d, cfg, "ES3 fragment texelFetch", 
      "#version 300 es\nprecision mediump float;\nuniform sampler2D uTex;\n"
      "out vec4 c;\nvoid main(){ c=texelFetch(uTex, ivec2(0,0), 0); }\n", 1);
    return 0;
}
