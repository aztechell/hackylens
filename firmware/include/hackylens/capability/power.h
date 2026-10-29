#ifndef HACKYLENS_CAPABILITY_POWER_H
#define HACKYLENS_CAPABILITY_POWER_H
#include "common.h"
#define HK_CAPABILITY_ID_POWER UINT32_C(0x00010006)
typedef struct hk_power hk_power_t;
enum { HK_POWER_FEATURE_OFF = 1U, HK_POWER_FEATURE_RESTART = 2U };
/* NULL when no power operations are available. */
const hk_power_t *hk_power_service(void);
/* Explicitly confirmed shutdown. HK_PENDING while runtime work is stopping;
 * repeat with the same absolute deadline. Successful power cut may not return. */
hk_result_t hk_power_request_off(const hk_power_t *power, hk_deadline_t deadline);
uint32_t hk_power_features(const hk_power_t *power);
hk_result_t hk_power_request_restart(const hk_power_t *power, hk_deadline_t deadline);
#endif
