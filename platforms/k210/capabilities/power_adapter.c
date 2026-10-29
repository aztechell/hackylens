#include "../../../firmware/src/capabilities/power_provider.h"
#include "../../../firmware/src/drivers/axp173.h"
#include "../../../firmware/src/services/power_shutdown.h"
static hk_result_t power_off(hk_deadline_t deadline)
{
    hk_result_t result = power_shutdown_prepare(deadline);
    if(result != HK_OK) return result;
    if(axp173_power_off()) return HK_OK;
    power_shutdown_restore();
    return HK_ERR_IO;
}
const hk_power_t hk_power_binding = {.request_off = power_off};
