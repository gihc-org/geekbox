/* fbdump.c — dump hele /dev/fb0 med mmap til en rå fil (1920x1080x2).
 *
 * Baggrund: /dev/fb0's read() giver kun 2.073.600 bytes (1 byte/pixel) —
 * mmap er den korrekte vej (M1-fælde, DOK §5.15a).
 *
 * Byg/afvikling på boksen:
 *   gcc -O2 -o /tmp/fbdump /tmp/fbdump.c
 *   /tmp/fbdump /root/skaerm.raw
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "brug: %s <udfil.raw>\n", argv[0]);
        return 1;
    }
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("/dev/fb0"); return 1; }
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fi)) { perror("ioctl"); return 1; }
    size_t len = (size_t)fi.line_length * vi.yres;
    unsigned char *fb = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }
    int out = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { perror(argv[1]); return 1; }
    if (write(out, fb, len) != (ssize_t)len)
        perror("write");
    close(out);
    printf("dumpet %zu bytes (%ux%u, stride %u) -> %s\n",
           len, vi.xres, vi.yres, fi.line_length, argv[1]);
    return 0;
}
