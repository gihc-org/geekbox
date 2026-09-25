/* fb_bench.c — hvor hurtigt kan CPU'en skrive til framebufferen? (25. sep 2026)
 *
 * Baggrund: praesentationsloftet maalt med fb_fps og raf_test_full.html er
 * ~0,2 us pr. pixel (2,4 skaerm-opdateringer/s ved 1920x1054). Spoergsmaalet er
 * om det er CPU'ens skrivning til /dev/fb0 (uncached/WC-hukommelse) eller
 * browserens eget male-arbejde. Dette værktøj maaler den raa skrivehastighed:
 * cached->cached (reference), cached->fb (memcpy), memset, og delvise
 * rektangler (som en dirty-rect-opdatering).
 *
 * Brug (som root):
 *   gcc -O2 -Wall -o fb_bench fb_bench.c
 *   ./fb_bench /dev/fb0            # skriver i en IKKE-vist buffer (sikkert)
 *   ./fb_bench /dev/fb0 visible    # skriver i den viste buffer (skaermen
 *                                  # flimrer indtil X tegner igen)
 *
 * Obs: skrivningen sker i fb'ens egen hukommelse; med 'visible' sker det i
 * buffer 0 (den LCDC viser). Uden 'visible' bruges den sidste bufferplads.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void report(const char *what, double ms, size_t bytes, size_t pixels)
{
    double mb = bytes / (1024.0 * 1024.0);
    printf("%-42s %8.1f ms  %8.1f MB/s  %6.3f us/pixel",
           what, ms, mb / (ms / 1000.0), ms * 1000.0 / (double)pixels);
    if (ms > 0.0)
        printf("  (%.1f fps hvis hele frameet)", 1000.0 / ms);
    printf("\n");
    fflush(stdout);
}

static void fill_pattern(uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(i * 7 + (i >> 8));
}

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
    int visible = (argc > 2 && strcmp(argv[2], "visible") == 0);
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "kan ikke aabne %s: %s\n", dev, strerror(errno));
        return 1;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
        perror("ioctl");
        return 1;
    }
    size_t bpp = var.bits_per_pixel / 8;
    size_t vframe = (size_t)fix.line_length * var.yres_virtual;
    size_t frame = (size_t)fix.line_length * var.yres;
    printf("fb %s: %ux%u (virt %ux%u) bpp=%u stride=%u smem=%u KB "
           "yoffset=%u\n",
           dev, var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           var.bits_per_pixel, fix.line_length, fix.smem_len / 1024,
           var.yoffset);
    printf("frame = %.2f MB, virt = %.2f MB, buffers = %u\n",
           frame / 1048576.0, vframe / 1048576.0,
           (unsigned)(fix.smem_len / frame));

    uint8_t *fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) {
        perror("mmap fb");
        return 1;
    }
    if (var.yoffset * fix.line_length + frame > fix.smem_len) {
        fprintf(stderr, "for lidt plads til en skjult buffer\n");
        return 1;
    }
    /* Skjult buffer: den sidste hele frame i den virtuelle flade. */
    uint8_t *dst = visible ? fb : fb + (fix.smem_len - frame);
    printf("skriver til %s buffer (offset %ld)\n", visible ? "den VISTE" : "en skjult",
           (long)(dst - fb));

    uint8_t *src = mmap(NULL, frame, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t *cached = mmap(NULL, frame, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (src == MAP_FAILED || cached == MAP_FAILED) {
        perror("mmap ram");
        return 1;
    }
    fill_pattern(src, frame);
    memset(cached, 0, frame);

    const int iters = 5;
    /* 1. Reference: cached -> cached, samme størrelse som et frame. */
    double t0 = now_ms();
    for (int i = 0; i < iters; i++)
        memcpy(cached, src, frame);
    report("cached -> cached, hele frame", (now_ms() - t0) / iters, frame,
           frame / bpp);

    /* 2. cached -> fb, hele frame. */
    t0 = now_ms();
    for (int i = 0; i < iters; i++)
        memcpy(dst, src, frame);
    report("cached -> fb, hele frame", (now_ms() - t0) / iters, frame,
           frame / bpp);

    /* 3. memset af hele fb-frame. */
    t0 = now_ms();
    for (int i = 0; i < iters; i++)
        memset(dst, 0x5a, frame);
    report("memset fb, hele frame", (now_ms() - t0) / iters, frame, frame / bpp);

    /* 4. Delvise rektangler (dirty-rect-størrelser): ligner en opdatering af
     *    kun det ændrede område. */
    struct { unsigned w, h; const char *navn; } rekt[] = {
        { 640, 360, "fb-rekt 640x360" },
        { 836, 470, "fb-rekt 836x470" },
        { 1280, 720, "fb-rekt 1280x720" },
        { 1920, 1054, "fb-rekt 1920x1054" },
    };
    for (unsigned r = 0; r < sizeof(rekt) / sizeof(rekt[0]); r++) {
        size_t rowbytes = (size_t)rekt[r].w * bpp;
        size_t bytes = rowbytes * rekt[r].h;
        if (rekt[r].w > var.xres || bytes > frame)
            continue;
        t0 = now_ms();
        for (int i = 0; i < iters; i++) {
            for (unsigned y = 0; y < rekt[r].h; y++)
                memcpy(dst + (size_t)y * fix.line_length, src, rowbytes);
        }
        report(rekt[r].navn, (now_ms() - t0) / iters, bytes, (size_t)rekt[r].w * rekt[r].h);
    }

    /* 5. 32-bit butiks-skrivning (som en naiv painter) i 640x360-rektanglet. */
    {
        uint32_t v = 0x12345678;
        size_t n = (size_t)640 * 360;
        t0 = now_ms();
        uint32_t *p = (uint32_t *)dst;
        for (size_t i = 0; i < n; i++)
            p[i] = v;
        report("32-bit stores, 640x360", now_ms() - t0, n * 4, n);
    }
    printf("(sidste: cachen ryddet, fb urørt uden for testbufferen)\n");
    return 0;
}
