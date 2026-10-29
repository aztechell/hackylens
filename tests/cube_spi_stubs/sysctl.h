#ifndef CUBE_TEST_SYSCTL_H
#define CUBE_TEST_SYSCTL_H
#include <stdint.h>
#define SYSCTL_CLOCK_CPU 0U
#define SYSCTL_DMA_CHANNEL_0 0U
#define SYSCTL_DMA_SELECT_SSI0_TX_REQ 0U
static inline void sysctl_dma_select(unsigned channel, unsigned request)
{ (void)channel; (void)request; }
static inline uint32_t sysctl_clock_get_freq(unsigned clock)
{
    (void)clock;
    return 400000000U;
}
static inline void sysctl_set_spi0_dvp_data(unsigned enabled) { (void)enabled; }
#endif
