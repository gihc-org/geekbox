/* ioctl_shim.c — logger ION_IOC_ALLOC-argumenter + resultat (LD_PRELOAD).
   For at se præcis hvilken anmodning gralloc laver, når den fejler. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>

struct ion_allocation_data { unsigned int len, align, heap_id_mask, flags, handle; };
#define ION_IOC_ALLOC _IOWR(0x49, 0, struct ion_allocation_data)

typedef int (*ioctl_fn)(int, unsigned long, ...);
static ioctl_fn real_ioctl = 0;

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (!real_ioctl) real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    int r = real_ioctl(fd, request, arg);
    if ((unsigned int)request == (unsigned int)ION_IOC_ALLOC && arg) {
        struct ion_allocation_data *a = (struct ion_allocation_data *)arg;
        fprintf(stderr, "[ion-shim] ALLOC fd=%d len=%u align=%u mask=0x%x flags=0x%x -> %d (%s)\n",
                fd, a->len, a->align, a->heap_id_mask, a->flags, r, strerror(errno));
    }
    return r;
}
