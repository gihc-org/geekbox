/* gralloc_test.c — tester 1.5-gralloc'ens alloc+lock med forskellige
 * format/usage-kombinationer. Mål: find hvorfor x11ws' lock giver EINVAL
 * ("gralloc lock fejlede (rc=-22)", 26. aug 2026). */
#include <android/hardware/gralloc.h>
#include <android/system/graphics.h>
#include <stdio.h>
#include <string.h>

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

    static const int fmts[] = { HAL_PIXEL_FORMAT_RGBA_8888,
                                HAL_PIXEL_FORMAT_RGB_565 };
    static const int usages[] = {
        GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_SW_READ_OFTEN,
        GRALLOC_USAGE_SW_READ_OFTEN,
        GRALLOC_USAGE_HW_FB,
        GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_SW_READ_OFTEN,
        0xCB,                                       /* x11ws' faktiske usage */
        0xCB | GRALLOC_USAGE_HW_FB,                 /* + HW_FB */
        0x40,                                       /* SW_READ_RARELY */
    };
    static const char *un[] = { "HW_FB|SW_RO", "SW_RO", "HW_FB", "HWC|SW_RO",
                                "0xCB", "0xCB|HW_FB", "SW_RR" };
    static const char *fn[] = { "RGBA_8888", "RGB_565" };

    for (int f = 0; f < 2; f++) {
    for (int u = 0; u < 7; u++) {
            buffer_handle_t h = NULL;
            int stride = 0;
            int rc = ad->alloc(ad, 320, 240, fmts[f], usages[u], &h, &stride);
            printf("%s %s: alloc rc=%d", fn[f], un[u], rc);
            if (rc != 0) { printf("\n"); continue; }
            void *ptr = NULL;
            int rc2 = gm->lock(gm, h, GRALLOC_USAGE_SW_READ_OFTEN,
                               0, 0, 320, 240, &ptr);
            printf(" lock rc=%d ptr=%s\n", rc2, ptr ? "JA" : "NEJ");
            if (rc2 == 0) {
                void *ptr2 = NULL;
                int rc3 = gm->lock(gm, h, usages[u], 0, 0, 320, 240, &ptr2);
                printf("   lock(full-usage %#x) rc=%d\n", usages[u], rc3);
                gm->unlock(gm, h);
            }
            ad->free(ad, h);
        }
    }
    return 0;
}
