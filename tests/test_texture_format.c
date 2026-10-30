#include "texture_format.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    const uint32_t formats[] = {28, 29, 61, 71, 72, 74, 75, 77, 78, 85, 86, 87, 88, 91, 115};
    for (unsigned i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
        UmiTexture t, p;
        assert(umi_tex_layout(7, 5, formats[i], &t));
        unsigned char header[148];
        uint32_t n = umi_dds_header(&t, header);
        unsigned char *b = calloc(1, n + t.bytes + 1);
        memcpy(b, header, n);
        assert(umi_dds_parse(b, n + t.bytes, &p));
        assert(p.width == 7 && p.height == 5 && p.format == formats[i]);
        assert(p.bytes == t.bytes && p.row == t.row && p.rows == t.rows);
        assert(!umi_dds_parse(b, n + t.bytes - 1, &p));
        assert(!umi_dds_parse(b, n + t.bytes + 1, &p));
        umi_tex_put(b + 7 * 4, 2);
        assert(!umi_dds_parse(b, n + t.bytes, &p));
        umi_tex_put(b + 7 * 4, 1);
        umi_tex_put(b + 28 * 4, 0x200);
        assert(!umi_dds_parse(b, n + t.bytes, &p));
        free(b);
    }
    UmiTexture a, b;
    assert(umi_tex_layout(3, 2, 28, &a));
    unsigned char tight[24], padded[32];
    for (unsigned i = 0; i < 24; i++)
        tight[i] = (unsigned char)i;
    memset(padded, 199, sizeof(padded));
    memcpy(padded, tight, 12);
    memcpy(padded + 16, tight + 12, 12);
    assert(umi_tex_hash(&a, tight, 12) == umi_tex_hash(&a, padded, 16));
    padded[16] ^= 1;
    assert(umi_tex_hash(&a, tight, 12) != umi_tex_hash(&a, padded, 16));
    assert(umi_tex_layout(6, 4, 28, &b) && umi_tex_scale(&a, &b));
    assert(umi_tex_layout(24, 16, 28, &b) && umi_tex_scale(&a, &b));
    assert(umi_tex_layout(27, 18, 28, &b) && !umi_tex_scale(&a, &b));
    assert(umi_tex_layout(6, 6, 28, &b) && !umi_tex_scale(&a, &b));
    assert(!umi_tex_layout(0, 5, 28, &b));
    assert(!umi_tex_layout(8193, 5, 28, &b));
    assert(!umi_tex_layout(8192, 8192, 28, &b));
    assert(!umi_tex_layout(4, 4, 999, &b));
    assert(umi_tex_layout(4096, 4096, 28, &b));
    unsigned char fuzz[512];
    uint32_t seed = 17;
    for (unsigned k = 0; k < 10000; k++) {
        for (unsigned i = 0; i < sizeof(fuzz); i++) {
            seed = seed * 1664525u + 1013904223u;
            fuzz[i] = (unsigned char)(seed >> 24);
        }
        assert(!umi_dds_parse(fuzz, k % sizeof(fuzz), &b));
    }
    puts("PASS: DDS format round-trips, padded upload hashing, scale limits, malformed/truncated/cube/mip "
         "rejection.");
}
