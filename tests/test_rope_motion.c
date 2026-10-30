#include "aspect_policy.h"
#include <assert.h>
#include <stdio.h>
static const float samples[2][16] = {
    {3.090890408e+00f, -6.492590910e-05f, -3.729145275e-03f, -3.716714680e-03f, 0.000000000e+00f,
     5.494926929e+00f, -3.189647105e-03f, -3.179014660e-03f, 1.148809493e-02f, 1.746842079e-02f,
     1.003332615e+00f, 9.999880791e-01f, 2.192518997e+01f, -2.200916672e+01f, 2.206018066e+01f,
     2.298664474e+01f},
    {3.090911865e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f,
     5.494954586e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f,
     1.003344536e+00f, 1.000000000e+00f, 2.192534065e+01f, -2.198686218e+01f, 2.207357788e+01f,
     2.300000000e+01f}};
int main(void) {
    for (unsigned n = 0; n < 2; n++) {
        float px, py, out[16];
        assert(umi_rope_camera(samples[n]));
        assert(umi_rope_scales(samples[n], &px, &py));
        assert(fabsf(py / px - 16.0f / 9) < 0.00001f);
        const float targets[] = {2.117647f, 1.411765f};
        for (unsigned t = 0; t < 2; t++) {
            assert(umi_correct_rope(samples[n], targets[t], out));
            float sx = fminf(1, (16.0f / 9) / targets[t]), sy = fminf(1, targets[t] / (16.0f / 9));
            for (unsigned j = 0; j < 4; j++) {
                assert(fabsf(out[4 * j] - samples[n][4 * j] * sx) < 0.00001f);
                assert(fabsf(out[4 * j + 1] - samples[n][4 * j + 1] * sy) < 0.00001f);
                assert(out[4 * j + 2] == samples[n][4 * j + 2]);
                assert(out[4 * j + 3] == samples[n][4 * j + 3]);
            }
        }
    }
    float bad[16];
    memcpy(bad, samples[0], 64);
    bad[8] += 0.5f;
    assert(!umi_rope_camera(bad));
    memcpy(bad, samples[0], 64);
    bad[2] += 0.1f;
    assert(!umi_rope_camera(bad));
    memcpy(bad, samples[0], 64);
    bad[3] = NAN;
    assert(!umi_rope_camera(bad));
    float mainCamera[48] = {0}, out[48];
    assert(!umi_correct_camera(mainCamera, 2, out));
    puts("PASS: captured moving/settled rope matrices, wide/tall corrections, depth preserved, malformed "
         "matrices rejected.");
}
