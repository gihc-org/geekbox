#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tæl ændrede 16-bpp-pixels mellem to rå XGetImage/fb0-filer (valgfrit
 * vindue-region). Brug til at bygge en tidslinje over skærmopdateringer:
 *   gcc -o rootdiff rootdiff.c
 *   rootdiff a.raw b.raw 1920 1080 [x0 y0 x1 y1]
 * Udskriver: changed, bbox */
int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "brug: rootdiff a.raw b.raw W H [x0 y0 x1 y1]\n");
        return 1;
    }
    FILE *fa = fopen(argv[1], "rb");
    FILE *fb = fopen(argv[2], "rb");
    if (!fa || !fb) { fprintf(stderr, "kan ikke åbne filer\n"); return 1; }
    int W = atoi(argv[3]), H = atoi(argv[4]);
    int x0 = (argc > 5) ? atoi(argv[5]) : 0;
    int y0 = (argc > 6) ? atoi(argv[6]) : 0;
    int x1 = (argc > 7) ? atoi(argv[7]) : W;
    int y1 = (argc > 8) ? atoi(argv[8]) : H;
    unsigned char *ra = malloc((size_t)W * H * 2);
    unsigned char *rb = malloc((size_t)W * H * 2);
    fread(ra, 1, (size_t)W * H * 2, fa);
    fread(rb, 1, (size_t)W * H * 2, fb);
    fclose(fa); fclose(fb);
    long changed = 0;
    int minx = W, miny = H, maxx = 0, maxy = 0;
    for (int y = y0; y < y1; y++) {
        const unsigned char *pa = ra + (size_t)y * W * 2;
        const unsigned char *pb = rb + (size_t)y * W * 2;
        for (int x = x0; x < x1; x++) {
            int i = x * 2;
            if (pa[i] != pb[i] || pa[i+1] != pb[i+1]) {
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
