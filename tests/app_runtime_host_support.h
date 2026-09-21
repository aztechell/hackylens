#ifndef HK_APP_RUNTIME_HOST_SUPPORT_H
#define HK_APP_RUNTIME_HOST_SUPPORT_H

#include "../firmware/src/app_runtime/switch.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Test support for compiling production app runtime on the host.
 * Provides clock, Time/Input/Display doubles, scope hooks, and observations.
 * Does not implement lifecycle states, unwind, teardown ordering, or generation.
 */
#define HK_APP_RUNTIME_HOST_TEARDOWN_BUDGET_US UINT64_C(100000)

typedef struct hk_app_runtime_host
{
    hk_app_switch_t switcher;
    uint64_t last_input_us;
    uint32_t prepare_calls;
    uint32_t cleanup_calls;
    uint32_t present_calls;
    uint32_t abort_calls;
    hk_deadline_t cleanup_deadline;
    hk_result_t fail_prepare_result;
    hk_result_t fail_cleanup_result;
    uint8_t batch_active;
} hk_app_runtime_host_t;

hk_result_t hk_app_runtime_host_init(hk_app_runtime_host_t *host);
hk_app_runtime_t *hk_app_runtime_host_runtime(hk_app_runtime_host_t *host);
hk_app_switch_t *hk_app_runtime_host_switch(hk_app_runtime_host_t *host);
uint64_t hk_app_runtime_host_now_us(const hk_app_runtime_host_t *host);
hk_result_t hk_app_runtime_host_set_now_us(
    hk_app_runtime_host_t *host, uint64_t now_us);
hk_result_t hk_app_runtime_host_advance_us(
    hk_app_runtime_host_t *host, uint64_t delta_us);
hk_result_t hk_app_runtime_host_push_input(
    hk_app_runtime_host_t *host, uint32_t raw_state);
void hk_app_runtime_host_fail_prepare(hk_app_runtime_host_t *host, hk_result_t result);
void hk_app_runtime_host_fail_cleanup(hk_app_runtime_host_t *host, hk_result_t result);
uint32_t hk_app_runtime_host_cleanup_calls(const hk_app_runtime_host_t *host);
hk_deadline_t hk_app_runtime_host_cleanup_deadline(const hk_app_runtime_host_t *host);
void hk_app_runtime_host_fill_app(
    hk_app_t *app,
    const char *id,
    const hk_app_v2_entry_t *entry,
    uint32_t state_bytes);

#ifdef __cplusplus
}
#endif

#endif
