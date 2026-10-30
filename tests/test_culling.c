#include "culling_policy.h"
#include <assert.h>
#include <stdio.h>
static int visible(const float *c, float x, float y, float z, float radius) {
    if (z <= 0)
        return 0;
    for (unsigned i = 48; i < 64; i += 4)
        if (c[i] * x + c[i + 1] * y + c[i + 2] * z + c[i + 3] > radius)
            return 0;
    return 1;
}
int main(void) {
    float c[64] = {0}, e[64];
    c[0] = 3.090911865f;
    c[5] = 5.494954586f;
    c[10] = 1.003344536f;
    c[11] = 1;
    c[14] = -1.003344536f;
    c[16] = c[21] = c[26] = c[31] = 1;
    c[32] = c[37] = c[42] = c[47] = 1;
    float nx = 1 / sqrtf(1 + 1 / (c[0] * c[0])), ny = 1 / sqrtf(1 + 1 / (c[5] * c[5]));
    c[48] = nx;
    c[50] = -nx / c[0];
    c[52] = -nx;
    c[54] = c[50];
    c[57] = ny;
    c[58] = -ny / c[5];
    c[61] = -ny;
    c[62] = c[58];
    assert(umi_expand_planes(c, 2.117647f, e));
    assert(!visible(c, 3.5f, 0, 10, 0));
    assert(visible(e, 3.5f, 0, 10, 0));
    assert(visible(e, -3.5f, 0, 10, 0));
    assert(!visible(e, 5, 0, 10, 0));
    assert(!visible(e, 0, 3, 10, 0));
    assert(!visible(e, 0, 0, -1, 0));
    assert(!memcmp(c, e, 192));
    assert(umi_expand_planes(c, 1.411765f, e));
    assert(!visible(c, 0, 2.1f, 10, 0));
    assert(visible(e, 0, 2.1f, 10, 0));
    assert(visible(e, 0, -2.1f, 10, 0));
    assert(!visible(e, 0, 3, 10, 0));
    for (unsigned i = 48; i < 64; i += 4)
        assert(fabsf(e[i] * e[i] + e[i + 1] * e[i + 1] + e[i + 2] * e[i + 2] - 1) < 1e-5f);
    assert(umi_expand_planes(c, c[5] / c[0], e));
    for (unsigned i = 0; i < 64; i++)
        assert(fabsf(c[i] - e[i]) < 1e-6f);
    assert(!umi_expand_planes(c, NAN, e));
    c[11] = 0;
    assert(!umi_expand_planes(c, 2, e));
    puts("PASS: wide/tall frusta admit expanded edges, retain rejection and normalized sphere planes; camera "
         "matrices unchanged.");
}
