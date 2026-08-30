#ifndef HK_RUNTIME_APP_RUNTIME_INTEGRATION_H
#define HK_RUNTIME_APP_RUNTIME_INTEGRATION_H

#include "../app_runtime/switch.h"

hk_result_t app_runtime_integration_initialize(void);
hk_result_t app_runtime_integration_open(
    const hk_app_t *app,
    const hk_input_snapshot_t *input);
hk_result_t app_runtime_integration_close(hk_app_stop_reason_t reason);
hk_result_t app_runtime_integration_input(
    const hk_input_event_t *input,
    uint8_t *consumed);
hk_result_t app_runtime_integration_media(
    hk_app_media_kind_t kind,
    uint32_t generation);
hk_result_t app_runtime_integration_wakeup(hk_app_wakeup_token_t token);
hk_result_t app_runtime_integration_poll(uint64_t now_us);
hk_result_t app_runtime_integration_now_us(uint64_t *now_us);
uint32_t app_runtime_integration_poll_interval_us(uint64_t now_us);
const hk_app_t *app_runtime_integration_active(void);

#endif
