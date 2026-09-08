/* scene_real_replay.c — afspil FANGEDE Subway Surfers-draws (vertex-/index-
 * buffere + uniform-matricer fra proxy-snapshots) på 1.5-stakken UDEN
 * Firefox/poki. Formål (8. sep 2026): store verdens-draws (prog7, count
 * 500-13000) har pre-post-diff=0 i live-Firefox, men offline clip-space-
 * beregning viser at dele af geometrien (fx q37/q38) ligger 100% INDE i
 * frustum. Tegner dataet + shaders + matricer overhovedet på 1.5?
 *
 * Byg/kør på boksen:
 *   gcc -O0 -g -o /root/scene_real_replay /root/scene_real_replay.c \
 *       -I/usr/local/include -L/opt/hybris -Wl,--no-as-needed \
 *       -l:libEGL_r.so -l:libGLESv2.so.2 -lhybris-common -landroid-properties \
 *       -ldl -lrt -lm
 *   LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 \
 *     DISPLAY=:0 /root/scene_real_replay /root/p7.vs /root/p7.fs \
 *     /tmp/vb_p7_q37_a0.bin /tmp/vb_p7_q37_a1.bin /tmp/vb_p7_q37_a2.bin \
 *     /tmp/eb_p7_q37.bin /tmp/mats_q37.txt 1515
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (GL_APIENTRY *PFN_DRAW_ELEMENTS_INSTANCED)(
    GLenum mode, GLsizei count, GLenum type, const void *indices,
    GLsizei instancecount);
typedef void (GL_APIENTRY *PFN_VERTEX_ATTRIB_DIVISOR)(GLuint, GLuint);
static PFN_DRAW_ELEMENTS_INSTANCED glDrawElementsInstanced_fn = NULL;
static PFN_VERTEX_ATTRIB_DIVISOR glVertexAttribDivisor_fn = NULL;

static char *read_file(const char *path, long *outlen)
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
    if (outlen)
        *outlen = n;
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

static void report_full(const char *tag, int w, int h)
{
    unsigned char *px = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLenum err = glGetError();
    const unsigned char *bg = px;
    int ndiff = 0, nonblk = 0, first = -1, last = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char *q = px + ((size_t)y * w + x) * 4;
            if (q[0] != bg[0] || q[1] != bg[1] || q[2] != bg[2])
                ndiff++;
            if (q[0] || q[1] || q[2])
                nonblk++;
        }
    }
    printf("PIXEL %s: hjørne=%d,%d,%d ndiff=%d/%d nonblack=%d/%d "
           "glerr=0x%x\n", tag, bg[0], bg[1], bg[2], ndiff, w * h,
           nonblk, w * h, (unsigned)err);
    for (int gy = 0; gy < 13; gy++) {
        int y = (gy * (h - 1)) / 12;
        for (int gx = 0; gx < 25; gx++) {
            int x = (gx * (w - 1)) / 24;
            const unsigned char *q = px + ((size_t)y * w + x) * 4;
            printf("%d,%d,%d ", q[0], q[1], q[2]);
        }
        printf("\n");
    }
    free(px);
    (void)first; (void)last;
}

int main(int argc, char **argv)
{
    if (argc < 9) {
        fprintf(stderr,
                "brug: %s <vs> <fs> <pos.bin> <nrm.bin> <uv.bin> <idx.bin> "
                "<matrices.txt> <count>\n", argv[0]);
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
    glDrawElementsInstanced_fn =
        (PFN_DRAW_ELEMENTS_INSTANCED)eglGetProcAddress(
            "glDrawElementsInstanced");
    glVertexAttribDivisor_fn = (PFN_VERTEX_ATTRIB_DIVISOR)
        eglGetProcAddress("glVertexAttribDivisor");
    printf("divisor-ptr: %p\n", (void *)glVertexAttribDivisor_fn);
    if (!glDrawElementsInstanced_fn) {
        fprintf(stderr, "glDrawElementsInstanced mangler\n");
        return 1;
    }

    const int W = 836, H = 470;   /* spillets canvas-størrelse */
    GLuint fbo, tex, depth;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth);
    glViewport(0, 0, W, H);
    printf("FBO-status: 0x%x %s\n", glCheckFramebufferStatus(GL_FRAMEBUFFER),
           glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE
               ? "KOMPLET" : "FEJL");

    /* hvid 1x1-tekstur til FS'ens sampler (spillet binder rigtige teksturer;
     * replay skal bare vise om rasterisering sker) */
    GLuint white;
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    unsigned char one[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, one);
    glActiveTexture(GL_TEXTURE0);

    char *vs = read_file(argv[1], NULL);
    char *fs = read_file(argv[2], NULL);
    GLuint prog = make_prog(vs, fs);
    glUseProgram(prog);

    long npos = 0, nnrm = 0, nuv = 0, nidx = 0;
    float *pos = (float *)read_file(argv[3], &npos);
    float *nrm = (float *)read_file(argv[4], &nnrm);
    float *uv = (float *)read_file(argv[5], &nuv);
    unsigned short *idx = (unsigned short *)read_file(argv[6], &nidx);
    npos /= 4; nnrm /= 4; nuv /= 4; nidx /= 2;
    int nverts = (int)(npos / 3);
    int count = atoi(argv[8]);
    if (count > (int)(nidx / 1)) count = (int)nidx;
    if (nverts > (int)(nnrm / 3)) nverts = (int)(nnrm / 3);
    if (nverts > (int)(nuv / 2)) nverts = (int)(nuv / 2);
    printf("data: nverts=%d count=%d idx=%ld (bytes p/n/u/i=%ld/%ld/%ld/%ld)\n",
           nverts, count, nidx / 1, npos * 4, nnrm * 4, nuv * 4, nidx * 2);

    float m1[16], m2[16], m3[16], off[2] = {0, 0};
    {
        char *mtxt = read_file(argv[7], NULL);
        float vals[64];
        int k = 0;
        char *p = mtxt;
        while (k < 64 && *p) {
            vals[k++] = strtof(p, &p);
            while (*p && (*p == ' ' || *p == '\n' || *p == '\t' ||
                          *p == ',' || *p == '\r'))
                p++;
        }
        if (k < 48) {
            fprintf(stderr, "matrix-fil: kun %d floats (skal være >= 48)\n", k);
            return 1;
        }
        memcpy(m1, vals, 16 * sizeof(float));
        memcpy(m2, vals + 16, 16 * sizeof(float));
        memcpy(m3, vals + 32, 16 * sizeof(float));
        if (k >= 50) {
            off[0] = vals[48];
            off[1] = vals[49];
        }
        free(mtxt);
    }
    printf("offset=%.4g,%.4g m1[12..14]=%.2f,%.2f,%.2f\n", off[0], off[1],
           m1[12], m1[13], m1[14]);

    glUniformMatrix4fv(u(prog, "webgl_1696719b288eb839"), 1, GL_FALSE, m1);
    glUniformMatrix4fv(u(prog, "webgl_a28a5227aa1e5d4d"), 1, GL_FALSE, m2);
    glUniformMatrix4fv(u(prog, "webgl_f1fef75e9d5d2054"), 1, GL_FALSE, m3);
    glUniform2f(u(prog, "webgl_ab2b4a2802248c73"), off[0], off[1]);
    glUniform4f(u(prog, "webgl_34ec1f38646ac536"), 0, 0, 1, 1);
    glUniform4f(u(prog, "webgl_2cf176d56275c2ff"), 1, 1, 1, 1);
    glUniform1i(u(prog, "webgl_201689f85ac140e"), 0);
    glUniform3f(u(prog, "webgl_16db7f01695ad16d"), 1, 1, 1);
    glUniform3f(u(prog, "webgl_993f9ca5bb1538d8"), 1, 1, 1);

    GLuint vb[3], ebo;
    glGenBuffers(3, vb);
    glGenBuffers(1, &ebo);
    GLint ploc = glGetAttribLocation(prog, "webgl_29688de933f0bd7a");
    GLint nloc = glGetAttribLocation(prog, "webgl_2414b4e816c3dcf8");
    GLint tloc = glGetAttribLocation(prog, "webgl_52e627521394104d");
    printf("attrib-locs pos=%d normal=%d uv=%d\n", ploc, nloc, tloc);
    glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)npos * 4, pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vb[1]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nnrm * 4, nrm, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vb[2]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nuv * 4, uv, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)nidx * 2, idx,
                 GL_STATIC_DRAW);
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
    const char *divs = getenv("REAL_DIVISOR");
    if (divs && ploc >= 0 && glVertexAttribDivisor_fn) {
        glVertexAttribDivisor_fn((GLuint)ploc, (GLuint)atoi(divs));
        printf("REAL_DIVISOR: pos divisor=%s (kollaps-test)\n", divs);
    }

    /* T1: state-frit (depth/cull/blend fra) */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(1.0f, 0.0f, 1.0f, 0.0f);   /* pink clear */
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElementsInstanced_fn(GL_TRIANGLES, (GLsizei)count,
                               GL_UNSIGNED_SHORT, NULL, 1);
    report_full("T1-real-stateoff-instanced", W, H);

    /* T2: spillets tilstand (depth LEQUAL + cull BACK, blend fra) */
    glClearColor(1.0f, 0.0f, 1.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDrawElementsInstanced_fn(GL_TRIANGLES, (GLsizei)count,
                               GL_UNSIGNED_SHORT, NULL, 1);
    report_full("T2-real-gamestate-instanced", W, H);

    /* T3: samme data, ikke-instanced (kontrol) */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(1.0f, 0.0f, 1.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_SHORT, NULL);
    report_full("T3-real-stateoff-elements", W, H);
    return 0;
}
