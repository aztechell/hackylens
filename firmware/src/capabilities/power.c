#include "power_provider.h"
#include <stddef.h>
const hk_power_t *hk_power_service(void)
{
    return (hk_power_binding.request_off || hk_power_binding.request_restart) ? &hk_power_binding : NULL;
}
hk_result_t hk_power_request_off(const hk_power_t *power, hk_deadline_t deadline)
{
    return power && power->request_off ? power->request_off(deadline) : HK_ERR_CAPABILITY_ABSENT;
}

uint32_t hk_power_features(const hk_power_t *power)
{
    return power ? (power->request_off ? HK_POWER_FEATURE_OFF : 0U) |
        (power->request_restart ? HK_POWER_FEATURE_RESTART : 0U) : 0U;
}
hk_result_t hk_power_request_restart(const hk_power_t *power, hk_deadline_t deadline)
{
    return power && power->request_restart ? power->request_restart(deadline) : HK_ERR_CAPABILITY_ABSENT;
}
