/* fb_fps.c - maaler skaermens FAKTISKE opdateringsrate ved at polle /dev/fb0.
 *
 * Baggrund (17. sep 2026): med software-layers kaldes eglSwapBuffers aldrig,
 * og sidens rAF-taelling viste 1,6/s i vores maalekoersler - mistanke om at
 * Firefox throttler rAF i denne opstilling, saa rAF er ikke noedvendigvis lig
 * skaermens opdateringsrate. Dette vaerktoej maaler i stedet direkte paa
 * framebufferen: det poller et antal blokke af det synlige billede og taeller
 * hvor ofte indholdet aendrer sig. Det er den rate brugeren ser.
 *
 * Vigtigt: med ypan er kun en del af framebufferen synlig; vaerktoejet bruger
 * var.yoffset * line_length som start, saa der maales paa det viste buffer.
 *
 * Byg/koer paa boksen:
 *   gcc -O2 -o /root/fb_fps /root/fb_fps.c
 *   /root/fb_fps [/dev/fb0] [sekunder] [poll-interval-ms]
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAXW 8
#define MAXH 6
#define BUFMAX 4096

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* op til fire buffere (ypan): hver buffer har sin egen historik, ellers giver
 * et buffer-flip falske "ændringer". X kan skrive i en anden buffer end den
 * foerste, saa vi maa se paa dem alle. */
#define MAXBUF 4
static unsigned char prev[MAXBUF][MAXW * MAXH][BUFMAX];
static int have[MAXBUF][MAXW * MAXH];
static unsigned long changes[MAXBUF][MAXW * MAXH];

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
    double secs = argc > 2 ? atof(argv[2]) : 10.0;
    int poll_ms = argc > 3 ? atoi(argv[3]) : 5;

    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        perror("open fb");
        return 1;
    }
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("ioctl FBIOGET_*");
        return 1;
    }
    printf("fb: %ux%u virt=%ux%u bpp=%u stride=%u smem_len=%u yoffset=%u "
           "xoffset=%u\n",
           vi.xres, vi.yres, vi.xres_virtual, vi.yres_virtual,
           vi.bits_per_pixel, fi.line_length, fi.smem_len, vi.yoffset,
           vi.xoffset);
    size_t vis = (size_t)fi.line_length * vi.yres;
    size_t len = fi.smem_len ? fi.smem_len : vis;
    size_t off = (size_t)fi.line_length * vi.yoffset;
    if (off + vis > len)
        off = 0;
    unsigned char *map = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap fb");
        return 1;
    }
    unsigned char *vis_p = map + off;
    int rows = (int)(vis / fi.line_length);
    int bw = vi.xres / MAXW;
    int bh = rows / MAXH;
    size_t bbytes = (size_t)bw * 2;
    if (bbytes > BUFMAX)
        bbytes = BUFMAX;
    printf("maaler %d sek, poll hver %d ms, blokke %dx%d (blok=%dx%d px)\n",
           (int)secs, poll_ms, MAXW, MAXH, bw, bh);

    unsigned long polls = 0;
    unsigned last_yoff = vi.yoffset;
    unsigned long yoff_changes = 0;
    double t0 = now_s();
    while (now_s() - t0 < secs) {
        /* Følg ypan: X kan flippe mellem to buffere, og så skal vi måle på den
         * buffer der ER synlig lige nu (ellers ser alt statisk ud). */
        struct fb_var_screeninfo vi2;
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vi2) == 0) {
            if (vi2.yoffset != last_yoff) {
                last_yoff = vi2.yoffset;
                yoff_changes++;
            }
            off = (size_t)fi.line_length * vi2.yoffset;
            if (off + vis > len)
                off = 0;
            vis_p = map + off;
        }
        unsigned slot = 0;
        {
            size_t bufsz = (size_t)fi.line_length * vi.yres;
            if (bufsz && (off / bufsz) < MAXBUF)
                slot = (unsigned)(off / bufsz);
        }
        if (slot >= MAXBUF)
            slot = 0;
        /* maal ogsaa de oevrige buffere, saa vi kan se om X skriver i en
         * anden buffer end den der vises lige nu */
        for (unsigned s = 0; s < MAXBUF; s++) {
            unsigned char *base = map + (size_t)s * (size_t)fi.line_length *
                                  vi.yres;
            if ((size_t)(base - map) + vis > len)
                break;
            for (int by = 0; by < MAXH; by++) {
                for (int bx = 0; bx < MAXW; bx++) {
                    int b = by * MAXW + bx;
                    unsigned char cur[BUFMAX];
                    size_t n = 0;
                    int y0 = by * bh;
                    for (int y = y0; y < y0 + bh && y < rows; y += 8) {
                        unsigned char *row = base +
                            (size_t)y * fi.line_length + (size_t)bx * bw * 2;
                        if (n + bbytes > sizeof cur)
                            break;
                        memcpy(cur + n, row, bbytes);
                        n += bbytes;
                    }
                    if (have[s][b] && n && memcmp(prev[s][b], cur, n) != 0)
                        changes[s][b]++;
                    memcpy(prev[s][b], cur, n);
                    have[s][b] = 1;
                }
            }
        }
#if 0
        for (int by = 0; by < MAXH; by++) {
            for (int bx = 0; bx < MAXW; bx++) {
                int b = by * MAXW + bx;
                unsigned char cur[BUFMAX];
                size_t n = 0;
                int y0 = by * bh;
                for (int y = y0; y < y0 + bh && y < rows; y += 8) {
                    unsigned char *row;
                    size_t take;
                    row = vis_p + (size_t)y * fi.line_length + (size_t)bx * bw * 2;
                    take = bbytes;
                    if (n + take > sizeof cur)
                        break;
                    memcpy(cur + n, row, take);
                    n += take;
                }
                if (have[slot][b] && n && memcmp(prev[slot][b], cur, n) != 0)
                    changes[slot][b]++;
                memcpy(prev[slot][b], cur, n);
                have[slot][b] = 1;
            }
        }
#endif
        polls++;
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (long)poll_ms * 1000000L;
            nanosleep(&ts, NULL);
        }
    }
    double dt = now_s() - t0;
    printf("polls=%lu paa %.2f s (ypan-skift set: %lu, sidste yoffset=%u)\n",
           polls, dt, yoff_changes, last_yoff);
    printf("(skift pr. buffer; 0 = buffer 0, 1 = naeste osv.)\n");
    for (int s = 0; s < MAXBUF; s++) {
        unsigned long tot = 0;
        for (int b = 0; b < MAXW * MAXH; b++)
            tot += changes[s][b];
        printf("buffer %d: %lu skift i alt", s, tot);
        for (int by = 0; by < MAXH; by++) {
            unsigned long rowsum = 0;
            for (int bx = 0; bx < MAXW; bx++)
                rowsum += changes[s][by * MAXW + bx];
            printf(" %lu", rowsum);
        }
        printf("\n");
    }
    unsigned long best = 0;
    int bb = -1;
    int bslot = 0;
    for (int s = 0; s < MAXBUF; s++)
        for (int b = 0; b < MAXW * MAXH; b++)
            if (changes[s][b] > best) {
                best = changes[s][b];
                bb = b;
                bslot = s;
            }
    printf("mest aktive blok: %d (bx=%d by=%d) skiftede %lu gange -> "
           "%.1f skaerm-opdateringer/s (buffer %d)\n",
           bb, bb % MAXW, bb / MAXW, best, (double)best / dt, bslot);
    munmap(map, len);
    close(fd);
    return 0;
}
