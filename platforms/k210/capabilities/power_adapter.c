#include "../../../firmware/src/capabilities/power_provider.h"
#include "board_config.h"
#include "hal_restart.h"
#if HK_BOARD_POWER_AXP173
#include "../../../firmware/src/drivers/axp173.h"
#endif
#include "../../../firmware/src/services/power_shutdown.h"
#if HK_BOARD_POWER_AXP173
static hk_result_t power_off(hk_deadline_t deadline)
{
    hk_result_t result = power_shutdown_prepare(deadline);
    if(result != HK_OK) return result;
    if(axp173_power_off()) return HK_OK;
    power_shutdown_restore();
    return HK_ERR_IO;
}
#endif
static hk_result_t power_restart(hk_deadline_t deadline)
{
    hk_result_t result = power_shutdown_prepare(deadline);
    if(result != HK_OK) return result;
    hal_restart();
    power_shutdown_restore();
    return HK_ERR_IO; /* Hardware reset must not return. */
}
const hk_power_t hk_power_binding = {
#if HK_BOARD_POWER_AXP173
    .request_off = power_off,
#endif
    .request_restart = power_restart,
};
