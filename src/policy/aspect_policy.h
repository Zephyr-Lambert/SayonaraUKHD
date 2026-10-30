#ifndef UMI_ASPECT_POLICY_H
#define UMI_ASPECT_POLICY_H
#include <math.h>
#include <string.h>
/* Matrices are row-major as verified by captured shader reflection. */
static int umi_camera(const float *m) {
    for (unsigned i = 0; i < 48; i++)
        if (!isfinite(m[i]))
            return 0;
    if (m[0] <= 0 || m[5] <= 0 || m[0] > 100 || m[5] > 100)
        return 0;
    const unsigned zeros[] = {1, 2, 3, 4, 6, 7, 8, 9, 12, 13, 15};
    for (unsigned i = 0; i < sizeof(zeros) / sizeof(*zeros); i++)
        if (fabsf(m[zeros[i]]) > 0.00001f)
            return 0;
    if (fabsf(m[14] - 1) > 0.00001f || m[10] < 1 || m[10] > 2 || m[11] >= 0)
        return 0;
    /* Validate the supplied combined matrix rather than assuming its contents. */
    for (unsigned r = 0; r < 4; r++)
        for (unsigned c = 0; c < 4; c++) {
            float v = 0;
            for (unsigned k = 0; k < 4; k++)
                v += m[r * 4 + k] * m[16 + k * 4 + c];
            if (fabsf(v - m[32 + r * 4 + c]) > 0.001f * (1 + fabsf(v)))
                return 0;
        }
    return 1;
}
static int umi_correct_camera(const float *src, float aspect, float *dst) {
    if (!umi_camera(src) || !isfinite(aspect) || aspect < 0.2f || aspect > 8)
        return 0;
    float native = src[5] / src[0];
    float x = aspect > native ? native / aspect : 1;
    float y = aspect < native ? aspect / native : 1;
    memcpy(dst, src, 192);
    for (unsigned i = 0; i < 4; i++) {
        dst[i] *= x;
        dst[4 + i] *= y;
        dst[32 + i] *= x;
        dst[36 + i] *= y;
    }
    return 1;
}
/* Column-major P*V with a rigid, possibly rotated view. The XYZ lengths
   of clip-X and clip-Y recover projection scales independently of tilt. */
static int umi_rope_scales(const float *m, float *px, float *py) {
    for (unsigned i = 0; i < 16; i++)
        if (!isfinite(m[i]))
            return 0;
    if (m[15] <= 0)
        return 0;
    float xx = 0, yy = 0, ww = 0, xy = 0, xw = 0, yw = 0, zw = 0;
    for (unsigned i = 0; i < 3; i++) {
        float x = m[4 * i], y = m[4 * i + 1], z = m[4 * i + 2], w = m[4 * i + 3];
        xx += x * x;
        yy += y * y;
        ww += w * w;
        xy += x * y;
        xw += x * w;
        yw += y * w;
        zw += z * w;
    }
    *px = sqrtf(xx);
    *py = sqrtf(yy);
    if (*px <= 0 || *py <= 0 || *px > 100 || *py > 100 || fabsf(ww - 1) > 0.0001f)
        return 0;
    if (fabsf(xy) > 0.0001f * (*px) * (*py) || fabsf(xw) > 0.0001f * (*px) || fabsf(yw) > 0.0001f * (*py))
        return 0;
    float depth = zw / ww;
    if (depth < 1 || depth > 2)
        return 0;
    for (unsigned i = 0; i < 3; i++)
        if (fabsf(m[4 * i + 2] - depth * m[4 * i + 3]) > 0.0001f)
            return 0;
    float nearTerm = m[14] - depth * m[15];
    return nearTerm < 0 && isfinite(nearTerm);
}
static int umi_rope_camera(const float *m) {
    float px, py;
    return umi_rope_scales(m, &px, &py);
}
static int umi_correct_rope(const float *src, float aspect, float *dst) {
    float px, py;
    if (!umi_rope_scales(src, &px, &py) || !isfinite(aspect) || aspect < 0.2f || aspect > 8)
        return 0;
    float native = py / px;
    float x = aspect > native ? native / aspect : 1;
    float y = aspect < native ? aspect / native : 1;
    memcpy(dst, src, 64);
    for (unsigned i = 0; i < 4; i++) {
        dst[i * 4] *= x;
        dst[i * 4 + 1] *= y;
    }
    return 1;
}
#endif
