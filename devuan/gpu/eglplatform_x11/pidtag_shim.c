/* pidtag_shim.c — LD_PRELOAD-shim der prefixer hver stderr/stdout-linje med
 * procesrolle + pid, så vi kan se HVILKEN Firefox-proces der logger hvad
 * (fx "x11ws: vindue pakket ind", "NewRenderer::Run is slow", "Killing GPU
 * process", "Exiting due to channel error"). Byg på boksen:
 *   gcc -O2 -shared -fPIC -o /root/pidtag_shim.so pidtag_shim.c -ldl
 *
 * Roller (fra /proc/self/cmdline):
 *   M  = main (parent)
 *   G  = gpu-process
 *   C  = content/tab
 *   S  = socket
 *   R  = rdd
 *   ?  = ukendt
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

static ssize_t (*real_write)(int, const void *, size_t);
static ssize_t (*real_writev)(int, const struct iovec *, int);

static char tag[16] = "? 0| ";
static int  tag_len = 5;

static void init_tag(void)
{
    char buf[512];
    int  fd, n = 0;
    char role = '?';

    fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
    }
    if (n > 0) {
        buf[n] = 0;
        /* argumenterne er NUL-adskilt — saml dem i én streng */
        for (int i = 0; i < n - 1; i++)
            if (buf[i] == 0)
                buf[i] = ' ';
        if (strstr(buf, "-contentproc") != NULL) {
            if (strstr(buf, " gpu") != NULL || strstr(buf, "gpu ") != NULL)
                role = 'G';
            else if (strstr(buf, "-isForBrowser") != NULL)
                role = 'C';
            else if (strstr(buf, "rdd") != NULL)
                role = 'R';
            else if (strstr(buf, "socket") != NULL)
                role = 'S';
            else
                role = 'c';
        } else {
            role = 'M';
        }
    }

    snprintf(tag, sizeof(tag), "%c %d| ", role, (int)getpid());
    tag_len = (int)strlen(tag);
}

__attribute__((constructor)) static void pidtag_init(void)
{
    init_tag();
}

/* Skriv én "linje" (op til første \n) med tag. Håndterer ikke delvise
 * skriv — godt nok til at identificere processer. */
static ssize_t tagged_write(int fd, const void *buf, size_t len)
{
    ssize_t out = 0;
    size_t  pos = 0;

    if (!real_write)
        real_write = dlsym(RTLD_NEXT, "write");
    if (!real_write)
        return write(fd, buf, len);

    if (fd != 1 && fd != 2) {
        return real_write(fd, buf, len);
    }

    while (pos < len) {
        const char *nl = memchr((const char *)buf + pos, '\n', len - pos);
        size_t chunk = nl ? (size_t)(nl - (const char *)buf - pos) + 1
                          : len - pos;

        real_write(fd, tag, tag_len);
        real_write(fd, (const char *)buf + pos, chunk);
        out += chunk;
        pos += chunk;
    }
    return out;
}

ssize_t write(int fd, const void *buf, size_t len)
{
    return tagged_write(fd, buf, len);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    int i;
    if (!real_writev)
        real_writev = dlsym(RTLD_NEXT, "writev");
    if (fd != 1 && fd != 2) {
        return real_writev ? real_writev(fd, iov, iovcnt) : -1;
    }
    for (i = 0; i < iovcnt; i++) {
        tagged_write(fd, iov[i].iov_base, iov[i].iov_len);
    }
    return 0;
}
