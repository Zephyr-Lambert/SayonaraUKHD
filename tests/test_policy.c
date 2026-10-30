#include <assert.h>
#include "scale_policy.h"
int main(void) {
    assert(umi_game_buffer(1024, 512, 28, 0x28, 1, 1, 1, 0));
    assert(umi_game_buffer(1024, 512, 45, 0x40, 1, 1, 1, 0));
    assert(!umi_game_buffer(1024, 512, 28, 0x8, 1, 1, 1, 0));   /* art texture */
    assert(!umi_game_buffer(968, 552, 28, 0x20, 1, 1, 1, 0));   /* output */
    assert(!umi_game_buffer(1024, 1024, 55, 0x40, 1, 1, 1, 0)); /* menu depth */
    assert(!umi_game_buffer(1024, 512, 28, 0x28, 1, 1, 1, 1));  /* uploaded data */
    assert(!umi_game_buffer(1024, 512, 28, 0x28, 2, 1, 1, 0));  /* mip chain */
    assert(!umi_game_buffer(1024, 512, 28, 0x28, 1, 2, 1, 0));  /* array */
    assert(!umi_game_buffer(1024, 512, 28, 0x28, 1, 1, 4, 0));  /* MSAA */
    assert(!umi_game_buffer(1024, 512, 71, 0x28, 1, 1, 1, 0));  /* wrong format */
}
