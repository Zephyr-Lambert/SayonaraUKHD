#ifndef UMI_SCALE_POLICY_H
#define UMI_SCALE_POLICY_H
#include <stdint.h>
/* Restrict the experiment to the two observed gameplay buffer descriptors.
   Uploaded art (initial data, SRV-only) and output/depth buffers are excluded. */
static int umi_game_buffer(uint32_t w, uint32_t h, uint32_t format, uint32_t bind, uint32_t mips,
                           uint32_t array, uint32_t samples, int has_data) {
    return w == 1024 && h == 512 && mips == 1 && array == 1 && samples == 1 && !has_data &&
           ((format == 28 && bind == 0x28) || (format == 45 && bind == 0x40));
}
#endif
