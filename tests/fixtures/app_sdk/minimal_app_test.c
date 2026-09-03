#include "app_runtime_host_support.h"

#include <stdio.h>

#include "minimal_private.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_start_render_storage[sizeof(minimal_state_t) + 64U];
static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_pending_storage[sizeof(minimal_state_t) + 64U];
static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_slow_tick_storage[sizeof(minimal_state_t) + 64U];

static hk_result_t start_requests_render(const hk_app_context_t *ctx)
{
    return hk_app_context_request_render(ctx, NULL);
}

static hk_result_t start_returns_pending(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_PENDING;
}

static hk_result_t consume_callback_budget(const hk_app_context_t *ctx)
{
    hk_time_t time;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_deadline_t wake;
    const char *app_id = NULL;
    uint32_t generation = 0U;

    if(hk_app_context_identity(
           ctx, &app_id, &generation, &owner) != HK_OK ||
       !app_id || generation == 0U || hk_owner_is_zero(owner) ||
       hk_app_context_time(ctx, 0U, &time) != HK_OK ||
       hk_time_deadline_after_us(owner, &time, 101U, &wake) != HK_OK)
        return HK_ERR_INTERNAL;
    return hk_time_sleep_until(owner, &time, wake, wake, NULL);
}

static hk_result_t slow_tick(const hk_app_context_t *ctx, uint64_t now_us)
{
    (void)now_us;
    return consume_callback_budget(ctx);
}

static int init_minimal(
    hk_app_runtime_host_t *host,
    hk_app_t *app,
    const hk_app_v2_entry_t *entry)
{
    CHECK(hk_app_runtime_host_init(host) == HK_OK);
    hk_app_runtime_host_fill_app(
        app, "minimal-fixture", entry, sizeof(minimal_state_t));
    return 0;
}

int main(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_app_switch_t *switcher;
    hk_app_v2_entry_t entry;
    uint8_t consumed = 0U;
    hk_input_event_t input = {0};
    uint32_t index;

    CHECK(HK_APP_SDK_VERSION_MAJOR == 0U);
    CHECK(HK_APP_SDK_VERSION_MINOR == 1U);
    CHECK(HK_APP_SDK_RUNTIME_MAXIMUM_EXCLUSIVE_MINOR == 2U);
    CHECK(HK_APP_SDK_MANIFEST_SCHEMA_MAJOR == 1U);

    CHECK(init_minimal(&host, &app, &minimal_app_entry) == 0);
    app.limits.tick_budget_us = app.limits.tick_interval_us + 1U;
    CHECK(hk_app_switch_open(
              hk_app_runtime_host_switch(&host), &app, NULL) ==
          HK_ERR_INVALID_ARGUMENT);

    CHECK(init_minimal(&host, &app, &minimal_app_entry) == 0);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_runtime_state(hk_app_runtime_host_runtime(&host)) ==
          HK_APP_RUNTIME_RUNNING);
    CHECK(minimal_app_check_time_contract());
    CHECK(minimal_app_check_display_contract());
    CHECK(minimal_app_check_stale_reacquire());
    CHECK(hk_app_runtime_host_push_input(&host, HK_INPUT_BUTTON_OK) == HK_OK);
    input.sequence = 1U;
    input.timestamp_us = hk_app_runtime_host_now_us(&host);
    input.state = HK_INPUT_BUTTON_OK;
    input.pressed = HK_INPUT_BUTTON_OK;
    CHECK(hk_app_switch_input(switcher, &input, &consumed) == HK_OK);
    CHECK(hk_app_switch_media(
              switcher, HK_APP_MEDIA_MOUNTED, 1U,
              hk_app_runtime_host_now_us(&host)) == HK_OK);
    CHECK(hk_app_runtime_host_advance_us(&host, 500U) == HK_OK);
    CHECK(hk_app_switch_poll(
              switcher, hk_app_runtime_host_now_us(&host)) == HK_OK);
    CHECK(hk_app_switch_poll(
              switcher, hk_app_runtime_host_now_us(&host)) == HK_OK);
    minimal_app_set_consume_input(0U);
    for(index = 0U; index < 9U; index++)
    {
        uint32_t state = (index & 1U) ?
            HK_INPUT_BUTTON_OK : HK_INPUT_BUTTON_LEFT;
        CHECK(hk_app_runtime_host_push_input(&host, state) == HK_OK);
    }
    CHECK(minimal_app_check_input_overflow(9U));
    CHECK(hk_app_switch_close(switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_runtime_state(hk_app_runtime_host_runtime(&host)) ==
          HK_APP_RUNTIME_INACTIVE);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);

    entry = minimal_app_entry;
    entry.state_storage = s_start_render_storage;
    entry.state_capacity_bytes = sizeof(s_start_render_storage);
    entry.start = start_requests_render;
    CHECK(init_minimal(&host, &app, &entry) == 0);
    CHECK(hk_app_switch_open(
              hk_app_runtime_host_switch(&host), &app, NULL) ==
          HK_ERR_INVALID_STATE);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);

    entry = minimal_app_entry;
    entry.state_storage = s_pending_storage;
    entry.state_capacity_bytes = sizeof(s_pending_storage);
    entry.start = start_returns_pending;
    CHECK(init_minimal(&host, &app, &entry) == 0);
    CHECK(hk_app_switch_open(
              hk_app_runtime_host_switch(&host), &app, NULL) ==
          HK_ERR_INVALID_STATE);

    entry = minimal_app_entry;
    entry.state_storage = s_slow_tick_storage;
    entry.state_capacity_bytes = sizeof(s_slow_tick_storage);
    entry.tick = slow_tick;
    CHECK(init_minimal(&host, &app, &entry) == 0);
    CHECK(hk_app_switch_open(
              hk_app_runtime_host_switch(&host), &app, NULL) == HK_OK);
    CHECK(hk_app_runtime_host_advance_us(&host, 500U) == HK_OK);
    CHECK(hk_app_switch_poll(
              hk_app_runtime_host_switch(&host),
              hk_app_runtime_host_now_us(&host)) == HK_ERR_DEADLINE_EXCEEDED);

    CHECK(init_minimal(&host, &app, &minimal_app_entry) == 0);
    hk_app_runtime_host_fail_acquire(
        &host, HK_CAPABILITY_ID_TIME, HK_ERR_IO);
    CHECK(hk_app_switch_open(
              hk_app_runtime_host_switch(&host), &app, NULL) == HK_ERR_IO);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);

    printf("APP_SDK_FIXTURE_OK\n");
    return 0;
}
