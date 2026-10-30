/* PNG replacement decoding: same stb_image configuration as the DLL (png_decode.h). */
#include "png_decode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned char *slurp(const char *path, int *size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    unsigned char *p = malloc((size_t)n);
    assert(p && fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (int)n;
    return p;
}
int main(void) {
    const char *names[] = {"rgba", "rgb", "grey", "grey_alpha", "palette", "grey16"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
        char png[256], raw[256];
        snprintf(png, sizeof(png), "tests/fixtures/%s.png", names[i]);
        snprintf(raw, sizeof(raw), "tests/fixtures/%s.rgba", names[i]);
        int n, rn, w, h, c;
        unsigned char *data = slurp(png, &n), *expected = slurp(raw, &rn);
        assert(stbi_info_from_memory(data, n, &w, &h, &c) && w == 6 && h == 4);
        unsigned char *pixels = stbi_load_from_memory(data, n, &w, &h, &c, 4);
        assert(pixels && w == 6 && h == 4 && rn == 6 * 4 * 4);
        assert(!memcmp(pixels, expected, (size_t)rn));
        stbi_image_free(pixels);
        for (int cut = 0; cut < n - 12; cut += 7) /* truncated files: rejected, never crash */
            assert(!stbi_load_from_memory(data, cut, &w, &h, &c, 4));
        data[n / 2] ^= 0x5a; /* corrupted data (CRC is not checked, so decoding may or may not fail) */
        pixels = stbi_load_from_memory(data, n, &w, &h, &c, 4);
        if (pixels)
            stbi_image_free(pixels);
        free(data);
        free(expected);
    }
    /* Header claiming 9000x9000: rejected from the header alone (STBI_MAX_DIMENSIONS 8192). */
    int n, w, h, c;
    unsigned char *data = slurp("tests/fixtures/rgba.png", &n);
    data[16] = 0, data[17] = 0, data[18] = 0x23, data[19] = 0x28;
    data[20] = 0, data[21] = 0, data[22] = 0x23, data[23] = 0x28;
    assert(!stbi_load_from_memory(data, n, &w, &h, &c, 4));
    unsigned char junk[256];
    for (unsigned k = 0; k < sizeof(junk); k++)
        junk[k] = (unsigned char)(k * 37 + 11);
    assert(!stbi_info_from_memory(junk, sizeof(junk), &w, &h, &c));
    free(data);
    puts("PASS: PNG RGBA/RGB/grey/grey+alpha/palette+tRNS/16-bit decode, truncation, oversize and junk rejection.");
}
