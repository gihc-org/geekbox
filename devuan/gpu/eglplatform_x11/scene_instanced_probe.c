/* scene_instanced_probe.c — isoleret test af glDrawElementsInstanced-stien
 * på 1.5-stakken UDEN poki/Firefox.
 *
 * Baggrund (8. sep 2026, cyan-scene): spillets store verdens-draws kører som
 * glDrawElementsInstanced(mode=TRIANGLES, count=500-13000, type=UNSIGNED_SHORT,
 * indices=0, primcount=1) mod canvas-FBO (fbo=3, 836x470). Matricerne er
 * plausible, drawBuffers=[COLOR_ATTACHMENT0], 0 GL-fejl — men postdraw-
 * readback viser ensartet himmel-cyan: meshene skriver 0 pixels.
 * scene_replay_probe.c beviste at prog7-VS+FS rasteriserer KORREKT via
 * glDrawArrays — men glDrawElementsInstanced-stien er aldrig blevet testet
 * isoleret. Denne probe sammenligner de tre draw-veje med samme shader/data:
 *   T1 glDrawArrays            (kendt-god baseline fra replay-proben)
 *   T2 glDrawElements          (med EBO, som spillet gør)
 *   T3 glDrawElementsInstanced primcount=1 (spillets sti)
 *   T4 som T3 med cull BACK (spillet har cull=1)
 *   T5 som T3 med depth-test (spillet har depthtest=1)
 *
 * Byg/kør på boksen (samme som scene_replay_probe):
 *   gcc -O0 -g -o /root/scene_instanced_probe /root/scene_instanced_probe.c \
 *       -I/usr/local/include -L/opt/hybris -lEGL -lhybris-common -ldl -lrt -lm
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
 *       EGL_PLATFORM=x11 DISPLAY=:0 /root/scene_instanced_probe /root/p7.vs /root/p7.fs
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (GL_APIENTRY *PFN_DRAW_ELEMENTS_INSTANCED)(
    GLenum mode, GLsizei count, GLenum type, const void *indices,
    GLsizei instancecount);
typedef void *(GL_APIENTRY *PFN_MAP_BUFFER_RANGE)(
    GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef GLboolean (GL_APIENTRY *PFN_UNMAP_BUFFER)(GLenum target);
static PFN_DRAW_ELEMENTS_INSTANCED glDrawElementsInstanced_fn = NULL;
static PFN_MAP_BUFFER_RANGE glMapBufferRange_fn = NULL;
static PFN_UNMAP_BUFFER glUnmapBuffer_fn = NULL;

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

static void setup_fbo(GLuint *fbo, GLuint *tex, GLuint *depth, int w, int h)
{
    glGenFramebuffers(1, fbo);
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, depth);
    glBindRenderbuffer(GL_RENDERBUFFER, *depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, *tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, *depth);
    glViewport(0, 0, w, h);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("FBO-status: 0x%x %s\n", st, st == GL_FRAMEBUFFER_COMPLETE ?
           "KOMPLET" : "FEJL");
}

static void report_pixels(const char *tag, int w, int h)
{
    unsigned char *px = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLenum err = glGetError();
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
    printf("PIXEL %s: hjørne=%d,%d,%d non-black-samples=%d/%d "
           "ndiff-vs-hjørne=%d glerr=0x%x\n",
           tag, first[0], first[1], first[2], nonbg, (w / 2) * (h / 2),
           ndiff, (unsigned)err);
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
    printf("instanced-ptr: %p\n",
           (void *)eglGetProcAddress("glDrawElementsInstanced"));
    printf("drawElements-ptr: %p\n",
           (void *)eglGetProcAddress("glDrawElements"));
    glDrawElementsInstanced_fn =
        (PFN_DRAW_ELEMENTS_INSTANCED)eglGetProcAddress(
            "glDrawElementsInstanced");
    if (!glDrawElementsInstanced_fn) {
        fprintf(stderr, "glDrawElementsInstanced findes IKKE via "
                        "eglGetProcAddress\n");
        return 1;
    }
    glMapBufferRange_fn = (PFN_MAP_BUFFER_RANGE)eglGetProcAddress(
        "glMapBufferRange");
    glUnmapBuffer_fn = (PFN_UNMAP_BUFFER)eglGetProcAddress(
        "glUnmapBuffer");
    printf("mapBufferRange-ptr: %p unmap-ptr: %p\n",
           (void *)glMapBufferRange_fn, (void *)glUnmapBuffer_fn);

    const int W = 512, H = 288;
    GLuint fbo, tex, depth;
    setup_fbo(&fbo, &tex, &depth, W, H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

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

    /* attributter i spillets rækkefølge: normal (loc0), pos (loc1), uv (loc2),
     * hver i egen VBO med stride=0 (spillet: buf 3/4/5, ptr=0). */
    GLfloat pos4[4 * 3] = {
        -0.8f, -0.8f, 0, 0.8f, -0.8f, 0, 0.8f, 0.8f, 0, -0.8f, 0.8f, 0
    };
    GLfloat uv4[4 * 2] = {0, 0, 1, 0, 1, 1, 0, 1};
    GLfloat nrm4[4 * 3] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    unsigned short idx[6] = {0, 1, 2, 0, 2, 3};
    GLuint vb[3], ebo;
    glGenBuffers(3, vb);
    glGenBuffers(1, &ebo);
    GLint ploc = glGetAttribLocation(prog, "webgl_29688de933f0bd7a");
    GLint nloc = glGetAttribLocation(prog, "webgl_2414b4e816c3dcf8");
    GLint tloc = glGetAttribLocation(prog, "webgl_52e627521394104d");
    printf("attrib-locs pos=%d normal=%d uv=%d\n", ploc, nloc, tloc);

    glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof pos4, pos4, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vb[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof nrm4, nrm4, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vb[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof uv4, uv4, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    if (ploc >= 0) {
        glEnableVertexAttribArray((GLuint)ploc);
        glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
        glVertexAttribPointer((GLuint)ploc, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    }
    if (nloc >= 0) {
        glEnableVertexAttribArray((GLuint)nloc);
        glBindBuffer(GL_ARRAY_BUFFER, vb[1]);
        glVertexAttribPointer((GLuint)nloc, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    }
    if (tloc >= 0) {
        glEnableVertexAttribArray((GLuint)tloc);
        glBindBuffer(GL_ARRAY_BUFFER, vb[2]);
        glVertexAttribPointer((GLuint)tloc, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    }

    /* Kan vi læse VBO/EBO-indhold tilbage via glMapBufferRange? (Proxyens
     * glGetBufferSubData er NULL på 1.5-stakken.) */
    if (glMapBufferRange_fn && glUnmapBuffer_fn) {
        glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
        float *mp = (float *)glMapBufferRange_fn(
            GL_ARRAY_BUFFER, 0, sizeof pos4, GL_MAP_READ_BIT);
        GLenum merr = glGetError();
        printf("map-vbo: ptr=%p glerr=0x%x ", (void *)mp, (unsigned)merr);
        if (mp) {
            for (int i = 0; i < 6 && i < 12; i++)
                printf("%.2f%s", mp[i], i < 11 ? "," : "\n");
            GLboolean um = glUnmapBuffer_fn(GL_ARRAY_BUFFER);
            printf("unmap-vbo=%d glerr=0x%x\n", (int)um,
                   (unsigned)glGetError());
        } else {
            printf("\n");
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        unsigned short *em = (unsigned short *)glMapBufferRange_fn(
            GL_ELEMENT_ARRAY_BUFFER, 0, sizeof idx, GL_MAP_READ_BIT);
        merr = glGetError();
        printf("map-ebo: ptr=%p glerr=0x%x ", (void *)em, (unsigned)merr);
        if (em) {
            for (int i = 0; i < 6; i++)
                printf("%u%s", em[i], i < 5 ? "," : "\n");
            glUnmapBuffer_fn(GL_ELEMENT_ARRAY_BUFFER);
            printf("unmap-ebo glerr=0x%x\n", (unsigned)glGetError());
        } else {
            printf("\n");
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        printf("glMapBufferRange/glUnmapBuffer mangler på stakken\n");
    }

    glClearColor(1.0f, 0.0f, 1.0f, 0.0f);  /* pink clear */
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
    report_pixels("T2-drawElements-EBO", W, H);

    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElementsInstanced_fn(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 1);
    report_pixels("T3-instanced-primcount1", W, H);

    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDrawElementsInstanced_fn(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 1);
    glDisable(GL_CULL_FACE);
    report_pixels("T4-instanced-cullBACK", W, H);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDrawElementsInstanced_fn(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 1);
    glDisable(GL_DEPTH_TEST);
    report_pixels("T5-instanced-depthLEQUAL", W, H);

    /* Kontrol: samme geometri med mange instanser (primcount=6) skal give
     * mere output end primcount=1, hvis instancing rent faktisk virker. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawElementsInstanced_fn(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, 6);
    report_pixels("T6-instanced-primcount6", W, H);

    return 0;
}
