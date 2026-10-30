#ifndef UMI_CULLING_POLICY_H
#define UMI_CULLING_POLICY_H
#include <math.h>
#include <string.h>
/* Camera layout recovered from its visibility and plane construction routines:
   column-major projection, view, inverse view, then four unit side planes. */
static int umi_expand_planes(const float *src, float aspect, float *dst) {
    if (!isfinite(aspect) || aspect < 0.2f || aspect > 8)
        return 0;
    for (unsigned i = 0; i < 64; i++)
        if (!isfinite(src[i]))
            return 0;
    if (src[0] <= 0 || src[5] <= 0 || src[0] > 100 || src[5] > 100 || fabsf(src[11] - 1) > 0.00001f ||
        fabsf(src[15]) > 0.00001f || src[10] < 1 || src[10] > 2 || src[14] >= 0)
        return 0;
    const unsigned zeros[] = {1, 2, 3, 4, 6, 7, 8, 9, 12, 13};
    for (unsigned i = 0; i < sizeof(zeros) / sizeof(*zeros); i++)
        if (fabsf(src[zeros[i]]) > 0.00001f)
            return 0;
    float native = src[5] / src[0];
    if (fabsf(native - 16.0f / 9) > 0.001f)
        return 0;
    float x = aspect > native ? native / aspect : 1, y = aspect < native ? aspect / native : 1;
    memcpy(dst, src, 256);
    for (unsigned i = 48; i < 64; i += 4) {
        float nx = src[i], ny = src[i + 1], nz = src[i + 2];
        float norm = nx * nx + ny * ny + nz * nz;
        if (norm < 0.98f || norm > 1.02f || nz >= 0 || fabsf(src[i + 3]) > 0.001f)
            return 0;
        nx *= x;
        ny *= y;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (!isfinite(len) || len < 0.00001f)
            return 0;
        dst[i] = nx / len;
        dst[i + 1] = ny / len;
        dst[i + 2] = nz / len;
        dst[i + 3] = src[i + 3] / len;
    }
    return 1;
}
#endif
