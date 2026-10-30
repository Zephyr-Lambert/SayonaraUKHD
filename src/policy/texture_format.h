#ifndef UMI_TEXTURE_FORMAT_H
#define UMI_TEXTURE_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define UMI_TEXTURE_MAX_BYTES (64u * 1024u * 1024u)
typedef struct {
    uint32_t width, height, format, row, rows, bytes, offset;
} UmiTexture;
static uint32_t umi_tex_u32(const void *p) {
    const unsigned char *b = p;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void umi_tex_put(void *p, uint32_t x) {
    unsigned char *b = p;
    for (unsigned i = 0; i < 4; i++)
        b[i] = (unsigned char)(x >> (i * 8));
}
static int umi_tex_layout(uint32_t w, uint32_t h, uint32_t f, UmiTexture *out) {
    if (!w || !h || w > 8192 || h > 8192)
        return 0;
    uint32_t unit = 0, block = 0;
    switch (f) {
        case 28:
        case 29:
        case 87:
        case 88:
        case 91:
            unit = 4;
            break;
        case 61:
            unit = 1;
            break;
        case 85:
        case 86:
        case 115:
            unit = 2;
            break;
        case 71:
        case 72:
            block = 8;
            break;
        case 74:
        case 75:
        case 77:
        case 78:
            block = 16;
            break;
        default:
            return 0;
    }
    uint32_t row = block ? ((w + 3) / 4) * block : w * unit, rows = block ? (h + 3) / 4 : h;
    uint64_t bytes = (uint64_t)row * rows;
    if (bytes > UMI_TEXTURE_MAX_BYTES)
        return 0;
    *out = (UmiTexture){w, h, f, row, rows, (uint32_t)bytes, 0};
    return 1;
}
static uint64_t umi_tex_hash(const UmiTexture *t, const void *data, uint32_t pitch) {
    uint64_t h = UINT64_C(14695981039346656037);
    const uint32_t values[] = {t->width, t->height, t->format};
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 4; j++)
            h = (h ^ ((values[i] >> (8 * j)) & 255)) * UINT64_C(1099511628211);
    const unsigned char *p = data;
    for (uint32_t y = 0; y < t->rows; y++)
        for (uint32_t x = 0; x < t->row; x++)
            h = (h ^ p[(size_t)y * pitch + x]) * UINT64_C(1099511628211);
    return h;
}
/* A single 2D mip. DX10 is used only for formats with no exact legacy tag. */
static uint32_t umi_dds_header(const UmiTexture *t, unsigned char header[148]) {
    uint32_t v[37] = {0};
    v[0] = 0x20534444;
    v[1] = 124;
    v[2] = 0x1007;
    v[3] = t->height;
    v[4] = t->width;
    v[7] = 1;
    v[19] = 32;
    v[27] = 0x1000;
    uint32_t n = 128;
    int block = t->format >= 71 && t->format <= 78;
    v[2] |= block ? 0x80000 : 8;
    v[5] = block ? t->bytes : t->row;
    switch (t->format) {
        case 71:
            v[20] = 4;
            v[21] = 0x31545844;
            break;
        case 74:
            v[20] = 4;
            v[21] = 0x33545844;
            break;
        case 77:
            v[20] = 4;
            v[21] = 0x35545844;
            break;
        case 28:
            v[20] = 0x41;
            v[22] = 32;
            v[23] = 255;
            v[24] = 0xff00;
            v[25] = 0xff0000;
            v[26] = 0xff000000;
            break;
        case 87:
        case 88:
            v[20] = t->format == 87 ? 0x41 : 0x40;
            v[22] = 32;
            v[23] = 0xff0000;
            v[24] = 0xff00;
            v[25] = 255;
            v[26] = t->format == 87 ? 0xff000000 : 0;
            break;
        case 61:
            v[20] = 0x20000;
            v[22] = 8;
            v[23] = 255;
            break;
        case 85:
            v[20] = 0x40;
            v[22] = 16;
            v[23] = 0xf800;
            v[24] = 0x7e0;
            v[25] = 0x1f;
            break;
        case 86:
            v[20] = 0x41;
            v[22] = 16;
            v[23] = 0x7c00;
            v[24] = 0x3e0;
            v[25] = 0x1f;
            v[26] = 0x8000;
            break;
        case 115:
            v[20] = 0x41;
            v[22] = 16;
            v[23] = 0xf00;
            v[24] = 0xf0;
            v[25] = 0xf;
            v[26] = 0xf000;
            break;
        default:
            v[20] = 4;
            v[21] = 0x30315844;
            v[32] = t->format;
            v[33] = 3;
            v[35] = 1;
            n = 148;
            break;
    }
    for (unsigned i = 0; i < 37; i++)
        umi_tex_put(header + i * 4, v[i]);
    return n;
}
static int umi_dds_parse(const void *data, size_t bytes, UmiTexture *t) {
    if (!data || bytes < 128 || bytes > UMI_TEXTURE_MAX_BYTES + 148u)
        return 0;
    const unsigned char *p = data;
    uint32_t v[37] = {0};
    for (unsigned i = 0; i < 32; i++)
        v[i] = umi_tex_u32(p + i * 4);
    if (v[0] != 0x20534444 || v[1] != 124 || v[19] != 32 || (v[2] & 0x1007) != 0x1007 || v[6] > 1 ||
        v[7] > 1 || v[28] || v[29] || v[30])
        return 0;
    uint32_t f = 0, offset = 128;
    if (v[20] & 4) {
        switch (v[21]) {
            case 0x31545844:
                f = 71;
                break;
            case 0x33545844:
                f = 74;
                break;
            case 0x35545844:
                f = 77;
                break;
            case 0x30315844:
                if (bytes < 148)
                    return 0;
                for (unsigned i = 32; i < 37; i++)
                    v[i] = umi_tex_u32(p + i * 4);
                if (v[33] != 3 || v[34] != 0 || v[35] != 1 || v[36] > 3)
                    return 0;
                f = v[32];
                offset = 148;
                break;
            default:
                return 0;
        }
    } else if ((v[20] & 0x20000) && v[22] == 8 && v[23] == 255 && !v[24] && !v[25] && !v[26])
        f = 61;
    else if (v[20] & 0x40) {
        if (v[22] == 32 && v[24] == 0xff00) {
            if (v[23] == 255 && v[25] == 0xff0000 && v[26] == 0xff000000)
                f = 28;
            if (v[23] == 0xff0000 && v[25] == 255 && v[26] == 0xff000000)
                f = 87;
            if (v[23] == 0xff0000 && v[25] == 255 && v[26] == 0)
                f = 88;
        } else if (v[22] == 16) {
            if (v[23] == 0xf800 && v[24] == 0x7e0 && v[25] == 0x1f && !v[26])
                f = 85;
            if (v[23] == 0x7c00 && v[24] == 0x3e0 && v[25] == 0x1f && v[26] == 0x8000)
                f = 86;
            if (v[23] == 0xf00 && v[24] == 0xf0 && v[25] == 0xf && v[26] == 0xf000)
                f = 115;
        }
    }
    if (!umi_tex_layout(v[4], v[3], f, t) || bytes != (uint64_t)offset + t->bytes)
        return 0;
    /* Refuse padded uncompressed DDS data; the converter writes tight rows. */
    if ((v[2] & 8) && v[5] != t->row)
        return 0;
    t->offset = offset;
    return 1;
}
static int umi_tex_scale(const UmiTexture *original, const UmiTexture *replacement) {
    if (replacement->width % original->width || replacement->height % original->height)
        return 0;
    uint32_t x = replacement->width / original->width, y = replacement->height / original->height;
    return x >= 1 && x <= 8 && x == y;
}
#endif
