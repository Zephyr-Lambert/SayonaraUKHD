#ifndef UMI_RESOLUTION_H
#define UMI_RESOLUTION_H
#include <stdint.h>
#include <wchar.h>
typedef struct {
    uint32_t width, height;
    float viewport;
} UmiSize;
/* Retain the game's 7/8 horizontal UV coverage. Raster targets are integer
   sized, but D3D11 viewports may have fractional widths. */
static int umi_size(uint32_t w, uint32_t h, UmiSize *out) {
    if (w < 320 || w > 7168 || h < 240 || h > 8192)
        return 0;
    out->width = (w * 8u + 6u) / 7u;
    out->height = h;
    out->viewport = (float)out->width * 0.875f;
    return 1;
}
static int umi_uint(const wchar_t *s, uint32_t *out) {
    uint32_t n = 0;
    if (!*s)
        return 0;
    for (; *s; s++) {
        if (*s < L'0' || *s > L'9' || n > 100000u)
            return 0;
        n = n * 10u + (uint32_t)(*s - L'0');
    }
    *out = n;
    return 1;
}
static int32_t umi_floor_edge(int32_t v, uint32_t size, uint32_t original) {
    return (int32_t)(((int64_t)v * size) / original);
}
static int32_t umi_ceil_edge(int32_t v, uint32_t size, uint32_t original) {
    return (int32_t)(((int64_t)v * size + original - 1) / original);
}
#endif
