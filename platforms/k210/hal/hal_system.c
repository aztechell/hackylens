#include "hal_system.h"

#include <sysctl.h>
#include <dmac.h>

void hal_system_init_clocks(void)
{
    sysctl_pll_set_freq(SYSCTL_PLL0, 800000000);
    /* Initialize once before display DMA0 and KPU DMA5 can be active. */
    dmac_init();
}
