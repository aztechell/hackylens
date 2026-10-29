#include "hal_restart.h"
#include "sysctl.h"
void hal_restart(void)
{
    sysctl_reset(SYSCTL_RESET_SOC);
    for(;;) {}
}
