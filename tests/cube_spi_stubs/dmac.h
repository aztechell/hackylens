#ifndef CUBE_TEST_DMAC_H
#define CUBE_TEST_DMAC_H
#include <stddef.h>
#define DMAC_CHANNEL0 0U
#define DMAC_ADDR_INCREMENT 0U
#define DMAC_ADDR_NOCHANGE 1U
#define DMAC_MSIZE_4 1U
#define DMAC_TRANS_WIDTH_32 2U
int dmac_is_idle(unsigned channel);
void dmac_channel_disable(unsigned channel);
void dmac_set_single_mode(unsigned channel, const void *src, void *dest,
                         unsigned src_inc, unsigned dest_inc, unsigned burst,
                         unsigned width, size_t count);
#endif
