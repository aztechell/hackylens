#ifndef HK_APP_RUNTIME_SWITCH_H
#define HK_APP_RUNTIME_SWITCH_H

#include "runtime_private.h"

typedef struct
{
    void *user;
    hk_result_t (*legacy_open)(void *user, const hk_app_t *app);
    hk_result_t (*legacy_close)(void *user, const hk_app_t *app);
    hk_result_t (*now_us)(void *user, uint64_t *now_us);
    hk_result_t (*render_begin)(
        void *user, const hk_app_runtime_t *runtime,
        hk_app_surface_t *surface);
    hk_result_t (*render_present)(
        void *user, hk_deadline_t deadline);
    hk_result_t (*render_abort)(void *user);
} hk_app_switch_ops_t;

typedef struct
{
    hk_app_runtime_t runtime;
    hk_app_runtime_ops_t runtime_ops;
    hk_app_switch_ops_t ops;
    const hk_app_t *active;
    hk_app_surface_t surface;
    uint64_t next_tick_us;
    hk_input_event_t pending_input;
    hk_app_stop_reason_t pending_close_reason;
    uint8_t transition_active;
    uint8_t opening;
    uint8_t pending_close;
    uint8_t pending_input_valid;
} hk_app_switch_t;

hk_result_t hk_app_switch_init(
    hk_app_switch_t *switcher,
    const hk_app_runtime_ops_t *runtime_ops,
    const hk_app_switch_ops_t *ops,
    uint64_t teardown_budget_us);
hk_result_t hk_app_switch_open(
    hk_app_switch_t *switcher,
    const hk_app_t *app,
    const hk_input_snapshot_t *legacy_input);
hk_result_t hk_app_switch_close(
    hk_app_switch_t *switcher,
    hk_app_stop_reason_t reason);
hk_result_t hk_app_switch_input(
    hk_app_switch_t *switcher,
    const hk_input_event_t *input,
    uint8_t *consumed);
hk_result_t hk_app_switch_media(
    hk_app_switch_t *switcher,
    hk_app_media_kind_t kind,
    uint32_t generation,
    uint64_t timestamp_us);
hk_result_t hk_app_switch_wakeup(
    hk_app_switch_t *switcher,
    hk_app_wakeup_token_t token,
    uint64_t timestamp_us);
hk_result_t hk_app_switch_poll(
    hk_app_switch_t *switcher,
    uint64_t now_us);
const hk_app_t *hk_app_switch_active(const hk_app_switch_t *switcher);
uint32_t hk_app_switch_poll_interval_us(
    const hk_app_switch_t *switcher,
    uint64_t now_us);

#endif
