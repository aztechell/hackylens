#ifndef HK_POWER_PROVIDER_H
#define HK_POWER_PROVIDER_H
#include <hackylens/capability/power.h>
struct hk_power {
    hk_result_t (*request_off)(hk_deadline_t deadline);
};
extern const hk_power_t hk_power_binding;
#endif
