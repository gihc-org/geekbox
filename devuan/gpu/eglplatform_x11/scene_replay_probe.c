/* scene_replay_probe.c — replay af Subway Surfers' fangede scene-shaders på
 * 1.5-stakken UDEN poki/Firefox. Formål (6. sep 2026, cyan-scene): efter
 * store verdens-draws (prog7/10/22) skriver FBO'et ingen pixels (ensartet
 * himmel-cyan målt via postdraw-readback). Dette probe kompilerer de samme
 * shader-kilder og tegner syntetisk geometri med identiske/matricer for at
 * isolere: (A) kan fragment-shaderen skrive? (B) kan VS+FS tilsammen
 * rasterisere?
 *
 * Byg/kør på boksen:
 *   gcc -O0 -g -o /root/scene_replay_probe /root/scene_replay_probe.c \
 *       -I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *       EGL_PLATFORM=x11 DISPLAY=:0 /root/scene_replay_probe /root/p7.vs /root/p7.fs
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "kan ikke åbne %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "læsefejl %s\n", path);
        exit(1);
    }
    b[n] = 0;
    fclose(f);
    return b;
}

static GLuint make_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[1024] = {0};
    glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
    if (!ok) {
        fprintf(stderr, "COMPILE-FEJL (%s): %s\n",
                type == GL_VERTEX_SHADER ? "VS" : "FS", log);
        exit(1);
    }
    if (log[0])
        printf("compile-info: %s\n", log);
    return s;
}

static GLuint make_prog(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, make_shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, make_shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    char log[1024] = {0};
    glGetProgramInfoLog(p, sizeof log - 1, NULL, log);
    if (!ok) {
        fprintf(stderr, "LINK-FEJL: %s\n", log);
        exit(1);
    }
    if (log[0])
        printf("link-info: %s\n", log);
    return p;
}

static GLint u(GLuint p, const char *n)
{
    return glGetUniformLocation(p, n);
}

static void mat_identity(GLfloat *m)
{
    for (int i = 0; i < 16; i++)
        m[i] = 0;
    for (int i = 0; i < 4; i++)
        m[i * 4 + i] = 1;
}

static void setup_fbo(GLuint *fbo, GLuint *tex, int w, int h)
{
    glGenFramebuffers(1, fbo);
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, *tex, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("FBO-status: 0x%x %s\n", st, st == GL_FRAMEBUFFER_COMPLETE ?
           "KOMPLET" : "FEJL");
}

static void report_pixels(const char *tag, int w, int h)
{
    unsigned char *px = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int nonbg = 0;
    unsigned char first[4] = {px[0], px[1], px[2], px[3]};
    int ndiff = 0;
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            unsigned char *q = px + ((size_t)y * w + x) * 4;
            if (q[0] != first[0] || q[1] != first[1] || q[2] != first[2])
                ndiff++;
            if (q[0] || q[1] || q[2])
                nonbg++;
        }
    }
    printf("PIXEL %s: hjørne=%d,%d,%d non-black-samples=%d/%d ndiff-vs-hjørne=%d\n",
           tag, first[0], first[1], first[2], nonbg, (w / 2) * (h / 2),
           ndiff);
    for (int gy = 0; gy < 9; gy++) {
        int y = (gy * (h - 1)) / 8;
        for (int gx = 0; gx < 17; gx++) {
            int x = (gx * (w - 1)) / 16;
            unsigned char *q = px + ((size_t)y * w + x) * 4;
            printf("%d,%d,%d ", q[0], q[1], q[2]);
        }
        printf("\n");
    }
    free(px);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "brug: %s <vs.glsl> <fs.glsl>\n", argv[0]);
        return 1;
    }
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, NULL, NULL);
    EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    eglChooseConfig(d, a, &cfg, 1, &n);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (c == EGL_NO_CONTEXT) {
        fprintf(stderr, "EGL ES3-kontekst fejlede\n");
        return 1;
    }
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("glGetBufferSubData ptr: %p\n",
           (void *)eglGetProcAddress("glGetBufferSubData"));

    const int W = 512, H = 288;
    GLuint fbo, tex;
    setup_fbo(&fbo, &tex, W, H);

    GLuint white;
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    unsigned char one[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, one);
    glActiveTexture(GL_TEXTURE0);

    char *vs = read_file(argv[1]);
    char *fs = read_file(argv[2]);
    GLuint prog = make_prog(vs, fs);
    glUseProgram(prog);

    /* Fallback-VS til FS-test: skal levere prog7-FS'ens ins */
    const char *fsvs =
        "#version 300 es\n"
        "in vec2 aP;\n"
        "out highp vec2 webgl_37cc066937ccaa46;\n"
        "out highp float webgl_6f6de9c7a1de796e;\n"
        "out highp vec3 webgl_20ebdbefc9b00237;\n"
        "out highp vec4 webgl_257ad0702aa8997c;\n"
        "void main(){ webgl_37cc066937ccaa46 = aP*0.5+0.5;"
        " webgl_6f6de9c7a1de796e = 0.0;"
        " webgl_20ebdbefc9b00237 = vec3(0.0,0.0,1.0);"
        " webgl_257ad0702aa8997c = vec4(0.0,0.0,0.0,1.0);"
        " gl_Position = vec4(aP,0.0,1.0); }\n";
    GLuint fsprog = make_prog(fsvs, fs);

    GLfloat tri[] = {-1, -1, 3, -1, -1, 3};
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    GLint aloc = glGetAttribLocation(fsprog, "aP");
    glEnableVertexAttribArray((GLuint)aloc);
    glVertexAttribPointer((GLuint)aloc, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    /* FS-test: tegner fuldskærms-trekant med spillets fragment-shader */
    glClearColor(1.0f, 0.0f, 1.0f, 0.0f);  /* pink clear */
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(fsprog);
    glUniform1i(u(fsprog, "webgl_201689f85ac140e"), 0);
    glUniform4f(u(fsprog, "webgl_2cf176d56275c2ff"), 1, 1, 1, 1);
    glUniform3f(u(fsprog, "webgl_16db7f01695ad16d"), 1, 1, 1);
    glUniform3f(u(fsprog, "webgl_993f9ca5bb1538d8"), 1, 1, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    report_pixels("FS-fuldskærm", W, H);

    /* Kombineret VS+FS med identitets-/synlige matricer + syntetisk kvad */
    glClearColor(1.0f, 0.0f, 1.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    GLfloat id[16];
    mat_identity(id);
    GLfloat proj[16];
    memset(proj, 0, sizeof proj);
    proj[0] = 1.0f;   /* x */
    proj[5] = 1.0f;   /* y */
    proj[10] = 1.0f;  /* z */
    proj[15] = 1.0f;
    glUniformMatrix4fv(u(prog, "webgl_1696719b288eb839"), 1, GL_FALSE, id);
    glUniformMatrix4fv(u(prog, "webgl_a28a5227aa1e5d4d"), 1, GL_FALSE, id);
    glUniformMatrix4fv(u(prog, "webgl_f1fef75e9d5d2054"), 1, GL_FALSE, proj);
    glUniform2f(u(prog, "webgl_ab2b4a2802248c73"), 0, 0);
    glUniform4f(u(prog, "webgl_34ec1f38646ac536"), 0, 0, 1, 1);
    glUniform4f(u(prog, "webgl_2cf176d56275c2ff"), 1, 1, 1, 1);
    glUniform1i(u(prog, "webgl_201689f85ac140e"), 0);
    glUniform3f(u(prog, "webgl_16db7f01695ad16d"), 1, 1, 1);
    glUniform3f(u(prog, "webgl_993f9ca5bb1538d8"), 1, 1, 1);

    /* attributter: pos (loc1), normal (loc0), uv (loc2) */
    GLfloat pbuf[6 * 3] = {
        -0.8f, -0.8f, 0, 0.8f, -0.8f, 0, 0.8f, 0.8f, 0,
        -0.8f, -0.8f, 0, 0.8f, 0.8f, 0, -0.8f, 0.8f, 0
    };
    GLfloat ubuf[6 * 2] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    GLuint vb[3];
    glGenBuffers(3, vb);
    glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof pbuf, pbuf, GL_STATIC_DRAW);
    GLint ploc = glGetAttribLocation(prog, "webgl_29688de933f0bd7a");
    GLint nloc = glGetAttribLocation(prog, "webgl_2414b4e816c3dcf8");
    GLint tloc = glGetAttribLocation(prog, "webgl_52e627521394104d");
    printf("attrib-locs pos=%d normal=%d uv=%d\n", ploc, nloc, tloc);
    if (ploc >= 0) {
        glEnableVertexAttribArray((GLuint)ploc);
        glVertexAttribPointer((GLuint)ploc, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    }
    if (nloc >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vb[1]);
        GLfloat norm[6 * 3];
        for (int i = 0; i < 18; i++)
            norm[i] = i % 3 == 2 ? 1.0f : 0.0f;
        glBufferData(GL_ARRAY_BUFFER, sizeof norm, norm, GL_STATIC_DRAW);
        glEnableVertexAttribArray((GLuint)nloc);
        glVertexAttribPointer((GLuint)nloc, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    }
    if (tloc >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vb[2]);
        glBufferData(GL_ARRAY_BUFFER, sizeof ubuf, ubuf, GL_STATIC_DRAW);
        glEnableVertexAttribArray((GLuint)tloc);
        glVertexAttribPointer((GLuint)tloc, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    report_pixels("VS+FS-kvad", W, H);
    return 0;
}
