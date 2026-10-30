#include "aspect_policy.h"
#include <assert.h>
#include <stdio.h>
static void make_camera(float *m) {
    memset(m, 0, 192);
    m[0] = 3.090912f;
    m[5] = m[0] * (16.0f / 9);
    m[10] = 1.003345f;
    m[11] = -1.003345f;
    m[14] = 1;
    m[16] = m[21] = m[26] = m[31] = 1;
    m[19] = 6.593486f;
    m[23] = -3.493908f;
    m[27] = 23;
    for (unsigned r = 0; r < 4; r++)
        for (unsigned c = 0; c < 4; c++)
            for (unsigned k = 0; k < 4; k++)
                m[32 + 4 * r + c] += m[4 * r + k] * m[16 + 4 * k + c];
}
int main(void) {
    float a[48], b[48];
    make_camera(a);
    assert(umi_camera(a));
    const float aspects[] = {16.0f / 9, 968.0f / 457.111f, 779.294f / 552, 32.0f / 9, 0.6f};
    for (unsigned t = 0; t < 5; t++) {
        assert(umi_correct_camera(a, aspects[t], b));
        assert(fabsf(b[5] / b[0] - aspects[t]) < 0.00001f);
        assert(b[0] <= a[0] + 0.00001f && b[5] <= a[5] + 0.00001f);
        assert(!memcmp(a + 16, b + 16, 64)); /* no camera movement */
        assert(!memcmp(a + 8, b + 8, 32));   /* depth unchanged */
        assert(umi_camera(b));               /* corrected combined matrix stays consistent */
    }
    /* Separate column-major line matrix must match the main 3D transform. */
    float line[16], fixed[16];
    for (unsigned r = 0; r < 4; r++)
        for (unsigned c = 0; c < 4; c++)
            line[c * 4 + r] = a[32 + r * 4 + c];
    for (unsigned t = 0; t < 5; t++) {
        assert(umi_correct_camera(a, aspects[t], b));
        assert(umi_correct_rope(line, aspects[t], fixed));
        for (unsigned r = 0; r < 4; r++)
            for (unsigned c = 0; c < 4; c++)
                assert(fabsf(fixed[c * 4 + r] - b[32 + r * 4 + c]) < 0.00001f);
        for (unsigned c = 0; c < 4; c++) {
            assert(fixed[c * 4 + 2] == line[c * 4 + 2]);
            assert(fixed[c * 4 + 3] == line[c * 4 + 3]);
        }
    }
    assert(!umi_correct_rope(line, NAN, fixed));
    float ui[16] = {0};
    ui[0] = ui[5] = ui[10] = ui[15] = 1;
    assert(!umi_rope_camera(ui));
    ui[0] = 2.0f / 896;
    ui[5] = -2.0f / 512;
    ui[12] = -1;
    ui[13] = 1;
    assert(!umi_rope_camera(ui));
    assert(!umi_correct_camera(a, 0, b));
    assert(!umi_correct_camera(a, NAN, b));
    a[15] = 1;
    assert(!umi_camera(a));
    make_camera(a);
    a[35] += 1;
    assert(!umi_camera(a));
    make_camera(a);
    a[0] = NAN;
    assert(!umi_camera(a));
    puts("PASS: expanded view, proportions, combined matrices, unchanged camera/depth, invalid inputs.");
}
