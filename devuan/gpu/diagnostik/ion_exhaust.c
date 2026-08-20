#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
struct ion_allocation_data { unsigned int len, align, heap_id_mask, flags, handle; };
#define ION_IOC_ALLOC _IOWR(0x49, 0, struct ion_allocation_data)
int main(void) {
    int fd = open("/dev/ion", 2);
    for (int i = 0; i < 10; i++) {
        struct ion_allocation_data a = { .len = 8294400, .align = 4096, .heap_id_mask = 0x10, .flags = 0 };
        errno = 0;
        int r = ioctl(fd, ION_IOC_ALLOC, &a);
        printf("#%d: ioctl=%d errno=%d (%s) handle=%u\n", i, r, errno, strerror(errno), a.handle);
    }
    return 0;
}
