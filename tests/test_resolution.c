#include <assert.h>
#include "resolution.h"
int main(void) {
    UmiSize s;
    uint32_t v;
    assert(umi_size(1792, 1024, &s) && s.width == 2048 && s.height == 1024 && s.viewport == 1792);
    assert(umi_size(2560, 1440, &s) && s.width == 2926 && s.viewport == 2560.25f);
    assert(umi_size(3840, 2160, &s) && s.width == 4389 && s.viewport == 3840.375f);
    assert(umi_size(7168, 8192, &s) && s.width == 8192);
    assert(!umi_size(7169, 1440, &s) && !umi_size(2560, 8193, &s));
    assert(!umi_size(0, 0, &s) && !umi_size(UINT32_MAX, 1440, &s));
    assert(umi_uint(L"2560", &v) && v == 2560);
    assert(!umi_uint(L"-1", &v) && !umi_uint(L"2560x", &v) && !umi_uint(L"", &v));
    assert(!umi_uint(L"99999999999999999999", &v));
    for (uint32_t w = 320; w <= 7168; w++) {
        assert(umi_size(w, 1440, &s));
        assert(s.viewport >= (float)w && s.viewport - (float)w < 1);
        assert(umi_floor_edge(0, s.width, 1024) == 0);
        assert(umi_ceil_edge(1024, s.width, 1024) == (int32_t)s.width);
        assert(umi_floor_edge(896, s.width, 1024) <= s.viewport);
        assert(umi_ceil_edge(896, s.width, 1024) >= s.viewport);
    }
}
