#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tæl ændrede pixels mellem to rå XGetImage/fb0-filer (16 eller 32 bpp,
 * valgfrit vindue-region). Brug til at bygge en tidslinje over skærmopdateringer:
 *   gcc -o rootdiff rootdiff.c
 *   rootdiff a.raw b.raw 1920 1080 [bpp] [x0 y0 x1 y1]
 * Udskriver: changed, bbox */
int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "brug: rootdiff a.raw b.raw W H [bpp] [x0 y0 x1 y1]\n");
        return 1;
    }
    FILE *fa = fopen(argv[1], "rb");
    FILE *fb = fopen(argv[2], "rb");
    if (!fa || !fb) { fprintf(stderr, "kan ikke åbne filer\n"); return 1; }
    int W = atoi(argv[3]), H = atoi(argv[4]);
    int bpp = (argc > 5) ? atoi(argv[5]) : 16;
    int n = 5 + (bpp != 16);
    int x0 = (argc > n) ? atoi(argv[n]) : 0;
    int y0 = (argc > n + 1) ? atoi(argv[n + 1]) : 0;
    int x1 = (argc > n + 2) ? atoi(argv[n + 2]) : W;
    int y1 = (argc > n + 3) ? atoi(argv[n + 3]) : H;
    int stride = W * (bpp / 8);
    unsigned char *ra = malloc((size_t)stride * H);
    unsigned char *rb = malloc((size_t)stride * H);
    fread(ra, 1, (size_t)stride * H, fa);
    fread(rb, 1, (size_t)stride * H, fb);
    fclose(fa); fclose(fb);
    long changed = 0;
    int minx = W, miny = H, maxx = 0, maxy = 0;
    for (int y = y0; y < y1; y++) {
        const unsigned char *pa = ra + (size_t)y * stride;
        const unsigned char *pb = rb + (size_t)y * stride;
        for (int x = x0; x < x1; x++) {
            int i = x * (bpp / 8);
            int diff = 0;
            for (int b = 0; b < bpp / 8; b++) if (pa[i + b] != pb[i + b]) { diff = 1; break; }
            if (diff) {
                changed++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    if (changed == 0) printf("%ld  (ingen ændringer)\n", changed);
    else printf("%ld  bbox=(%d,%d)-(%d,%d)\n", changed, minx, miny, maxx, maxy);
    return 0;
}
