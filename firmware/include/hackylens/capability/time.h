#ifndef HACKYLENS_CAPABILITY_TIME_H
#define HACKYLENS_CAPABILITY_TIME_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_TIME UINT32_C(0x00010001)
#define HK_TIME_FEATURE_MONOTONIC_US (UINT64_C(1) << 0)
#define HK_TIME_FEATURE_SLEEP_UNTIL (UINT64_C(1) << 1)
#define HK_TIME_FEATURES_0_1 \
    (HK_TIME_FEATURE_MONOTONIC_US | HK_TIME_FEATURE_SLEEP_UNTIL)
#define HK_TIME_LIMIT_MAX_SLEEP_US UINT32_C(1)
#define HK_TIME_MAX_SLEEP_US UINT64_C(300000000)
#define HK_TIME_CANCEL_PROBE_MAX_US UINT64_C(5000)

/* Immutable board binding, required by the firmware runtime. */
typedef struct hk_time hk_time_t;
const hk_time_t *hk_time_service(void);

hk_result_t hk_time_now_us(
    const hk_time_t *handle,
    uint64_t *value);
hk_result_t hk_time_deadline_after_us(
    const hk_time_t *handle,
    uint64_t duration_us,
    hk_deadline_t *deadline);
hk_result_t hk_time_sleep_until(
    const hk_time_t *handle,
    hk_deadline_t wake_target,
    hk_deadline_t operation_deadline,
    const hk_cancel_t *cancel);

#ifdef __cplusplus
}
#endif

#endif
