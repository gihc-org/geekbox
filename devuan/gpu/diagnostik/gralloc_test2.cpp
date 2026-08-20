#include <cstddef>
#include <android/hardware/hardware.h>
#include <android/hardware/hwcomposer.h>
#include <android/hardware/gralloc.h>
#include <stdio.h>
int main(void) {
    // 1. hwc først — som i test_triangle
    const hw_module_t *hwc_mod = NULL;
    hwc_composer_device_1_t *hwc = NULL;
    if (hw_get_module(HWC_HARDWARE_MODULE_ID, &hwc_mod) != 0) { printf("hwc_get fejlede\n"); return 1; }
    printf("hwc-modul: %s\n", hwc_mod->name ? hwc_mod->name : "?");
    if (hwc_open_1(hwc_mod, &hwc) != 0) { printf("hwc_open_1 fejlede\n"); return 2; }
    printf("hwc åbnet\n");
    // 2. gralloc — som i test_triangle
    const hw_module_t *mod = NULL;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mod) != 0) { printf("gralloc_get fejlede\n"); return 3; }
    printf("gralloc-modul: %s\n", mod->name ? mod->name : "?");
    const gralloc_module_t *gmod = (const gralloc_module_t *)mod;
    alloc_device_t *alloc = NULL;
    if (gmod->common.methods->open((hw_module_t *)gmod, GRALLOC_HARDWARE_GPU0, (hw_device_t **)&alloc) != 0) { printf("open gpu0 fejlede\n"); return 4; }
    // 3. allokér som vinduet gør
    for (int u = 0; u < 2; u++) {
        unsigned int usage = u == 0 ? GRALLOC_USAGE_HW_FB : (GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_FB);
        buffer_handle_t handle = NULL; int stride = 0;
        int r = alloc->alloc(alloc, 1920, 1080, HAL_PIXEL_FORMAT_RGBA_8888, usage, &handle, &stride);
        printf("alloc usage=0x%x -> r=%d (%s) stride=%d\n", usage, r, r==0?"OK":"FEJL", stride);
    }
    return 0;
}
