/* gralloc_test3.c — 26. aug 2026: isolér hvilken intern check i 1.5-gralloc'ens
 * lock der fejler for ikke-root (kristian). Byg på boksen:
 *   gcc -O2 -o /usr/local/bin/gralloc_test3 gralloc_test3.c \
 *       -I/usr/local/include -L/opt/hybris -lhardware -lhybris-common -ldl
 */
#include <android/hardware/gralloc.h>
#include <android/system/graphics.h>
#include <stdio.h>
#include <string.h>

static void dump_handle(const char *tag, buffer_handle_t h)
{
    if (!h) { printf("%s: handle=NULL\n", tag); return; }
    const int *d = (const int *)h;
    int nf = d[1], ni = d[2];
    printf("%s: handle=%p v=%d f=%d i=%d data[0..%d]=", tag, h, d[0], nf, ni,
           (nf + ni) < 12 ? (nf + ni) : 11);
    for (int i = 3; i < 3 + (nf + ni < 12 ? nf + ni : 11); i++)
        printf("%d%s", d[i], i + 1 < 3 + (nf + ni < 12 ? nf + ni : 11) ? "," : "");
    printf("\n");
}

int main(void)
{
    const hw_module_t *mod = NULL;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mod) != 0) {
        printf("hw_get_module(gralloc) fejlede\n");
        return 1;
    }
    gralloc_module_t const *gm = (gralloc_module_t const *)mod;
    alloc_device_t *ad = NULL;
    if (gralloc_open(mod, &ad) != 0) {
        printf("gralloc_open fejlede\n");
        return 1;
    }
    printf("module=%p id=%s name=%s lockptr=%p registerBuffer=%p "
           "unregisterBuffer=%p euid=%d\n",
           (const void *)gm, gm->common.id ? gm->common.id : "?",
           gm->common.name ? gm->common.name : "?",
           (void *)gm->lock, (void *)gm->registerBuffer,
           (void *)gm->unregisterBuffer, (int)geteuid());

    /* Kombinationer der matcher x11ws: fmt RGBA_8888 (1), alloc-usage 0x118,
     * lock-usage 0x80 (SW_READ_OFTEN). */
    static const int fmts[] = { 1, 1, 1, 4 };
    static const int aus[]  = { 0x118, 0x118, 0x80, 0x80 };
    static const int lus[]  = { 0x80, 0x3, 0x80, 0x80 };
    static const char *un[] = { "x11ws(0x118/0x80)", "0x118/0x3",
                                "0x80/0x80", "565/0x80" };

    for (int i = 0; i < 4; i++) {
        buffer_handle_t h = NULL;
        int stride = 0;
        int rc = ad->alloc(ad, 320, 240, fmts[i], aus[i], &h, &stride);
        printf("[%s] alloc rc=%d", un[i], rc);
        if (rc != 0 || !h) { printf("\n"); continue; }
        dump_handle("  handle", h);

        void *ptr = NULL;
        int rc2 = gm->lock(gm, h, lus[i], 0, 0, 320, 240, &ptr);
        printf("  lock rc=%d ptr=%s\n", rc2, ptr ? "JA" : "NEJ");
        if (rc2 == 0) gm->unlock(gm, h);

        /* Test A: registerBuffer FØR lock (mapper/register-hypotese) */
        if (gm->registerBuffer) {
            int rrc = gm->registerBuffer(gm, h);
            void *ptr2 = NULL;
            int rc3 = gm->lock(gm, h, lus[i], 0, 0, 320, 240, &ptr2);
            printf("  registerBuffer rc=%d lock-efter-register rc=%d ptr=%s\n",
                   rrc, rc3, ptr2 ? "JA" : "NEJ");
            if (rc3 == 0) {
                gm->unlock(gm, h);
                gm->unregisterBuffer(gm, h);
            }
        }
        ad->free(ad, h);
    }
    return 0;
}
