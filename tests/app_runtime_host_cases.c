#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
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

typedef enum
{
    FAIL_NONE = 0,
    FAIL_GRANT_TIME,
    FAIL_GRANT_INPUT,
    FAIL_GRANT_DISPLAY,
    FAIL_GRANT_SERVICE,
    FAIL_START,
    FAIL_EVENT,
    FAIL_TICK,
    FAIL_RENDER,
    FAIL_STOP,
    FAIL_OWNER_CLEANUP,
    FAIL_START_RENDER,
    FAIL_START_PENDING,
    FAIL_SLOW_TICK,
    FAIL_SLOW_RENDER,
    FAIL_HOLD_TIME,
} fail_point_t;

typedef struct
{
    fail_point_t fail;
    uint32_t stop_calls;
    hk_app_stop_reason_t stop_reason;
    hk_deadline_t stop_deadline;
    hk_app_context_t copied;
    hk_app_wakeup_token_t token;
} simple_app_t;

static simple_app_t s_simple;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_simple_state[64];
static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_alt_storage[sizeof(minimal_state_t) + 64U];

static hk_result_t consume_budget(const hk_app_context_t *ctx)
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

static hk_result_t simple_start(const hk_app_context_t *ctx)
{
    uint8_t available = 0U;
    const char *fallback = NULL;

    s_simple.copied = *ctx;
    if(hk_app_context_capability_status(
           ctx, HK_CAPABILITY_ID_TIME, 0U, &available, &fallback) != HK_OK ||
       !available)
        return HK_ERR_INTERNAL;
    if(s_simple.fail == FAIL_START_RENDER)
        return hk_app_context_request_render(ctx, NULL);
    if(s_simple.fail == FAIL_START_PENDING)
        return HK_PENDING;
    return s_simple.fail == FAIL_START ? HK_ERR_IO : HK_OK;
}

static hk_result_t simple_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    if(event->kind == HK_APP_EVENT_RUNTIME_CLOSE)
        s_simple.stop_reason = event->data.close.reason;
    if(event->kind == HK_APP_EVENT_INPUT &&
       hk_app_context_wakeup_token(ctx, 7U, &s_simple.token) != HK_OK)
        return HK_ERR_INTERNAL;
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(s_simple.fail == FAIL_SLOW_TICK)
            return consume_budget(ctx);
        return s_simple.fail == FAIL_TICK ? HK_ERR_IO : HK_OK;
    }
    return s_simple.fail == FAIL_EVENT ? HK_ERR_IO : HK_OK;
}

static hk_result_t simple_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)surface;
    if(s_simple.fail == FAIL_SLOW_RENDER)
        return consume_budget(ctx);
    return s_simple.fail == FAIL_RENDER ? HK_ERR_IO : HK_OK;
}

static hk_result_t simple_stop(const hk_app_context_t *ctx)
{
    s_simple.stop_calls++;
    if(hk_app_context_teardown_deadline(ctx, &s_simple.stop_deadline) != HK_OK)
        return HK_ERR_INTERNAL;
    if(s_simple.fail == FAIL_HOLD_TIME)
        return HK_OK;
    return s_simple.fail == FAIL_STOP ? HK_ERR_IO : HK_OK;
}

static const hk_app_v2_entry_t s_simple_entry = {
    .state_storage = s_simple_state,
    .state_capacity_bytes = sizeof(s_simple_state),
    .start = simple_start,
    .event = simple_event,
    .render = simple_render,
    .stop = simple_stop,
};

static int reset_simple(hk_app_runtime_host_t *host, hk_app_t *app)
{
    memset(&s_simple, 0, sizeof(s_simple));
    CHECK(hk_app_runtime_host_init(host) == HK_OK);
    hk_app_runtime_host_fill_app(
        app, "host-simple", &s_simple_entry, sizeof(s_simple_state));
    return 0;
}

static int open_app(
    hk_app_runtime_host_t *host, const hk_app_t *app, hk_result_t expected)
{
    hk_result_t result = hk_app_switch_open(
        hk_app_runtime_host_switch(host), app, NULL);

    CHECK(result == expected);
    return 0;
}

static int check_inactive(hk_app_runtime_host_t *host, hk_result_t first)
{
    hk_app_runtime_t *runtime = hk_app_runtime_host_runtime(host);

    CHECK(hk_app_runtime_state(runtime) == HK_APP_RUNTIME_INACTIVE);
    CHECK(hk_app_runtime_first_error(runtime) == first);
    CHECK(hk_app_switch_active(hk_app_runtime_host_switch(host)) == NULL);
    return 0;
}

static int check_failure_point(fail_point_t point)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_result_t expected = HK_ERR_IO;
    uint32_t owner_calls = 1U;
    uint8_t launch_fails = 0U;

    CHECK(reset_simple(&host, &app) == 0);
    s_simple.fail = point;
    if(point == FAIL_GRANT_TIME)
        hk_app_runtime_host_fail_acquire(
            &host, HK_CAPABILITY_ID_TIME, HK_ERR_IO);
    else if(point == FAIL_GRANT_INPUT)
        hk_app_runtime_host_fail_acquire(
            &host, HK_CAPABILITY_ID_INPUT, HK_ERR_IO);
    else if(point == FAIL_GRANT_DISPLAY)
        hk_app_runtime_host_fail_acquire(
            &host, HK_CAPABILITY_ID_DISPLAY, HK_ERR_IO);
    else if(point == FAIL_GRANT_SERVICE)
        hk_app_runtime_host_fail_service(&host, HK_ERR_IO);
    else if(point == FAIL_OWNER_CLEANUP)
        hk_app_runtime_host_fail_owner_cleanup(&host, HK_ERR_IO);
    launch_fails = (uint8_t)(
        point == FAIL_START || point == FAIL_GRANT_TIME ||
        point == FAIL_GRANT_INPUT || point == FAIL_GRANT_DISPLAY ||
        point == FAIL_GRANT_SERVICE);
    if(launch_fails)
        CHECK(open_app(&host, &app, expected) == 0);
    else
    {
        CHECK(open_app(&host, &app, HK_OK) == 0);
        if(point == FAIL_EVENT)
            CHECK(hk_app_switch_media(
                      hk_app_runtime_host_switch(&host),
                      HK_APP_MEDIA_ERROR, 9U,
                      hk_app_runtime_host_now_us(&host)) == expected);
        else if(point == FAIL_TICK)
        {
            CHECK(hk_app_runtime_host_advance_us(&host, 500U) == HK_OK);
            CHECK(hk_app_switch_poll(
                      hk_app_runtime_host_switch(&host),
                      hk_app_runtime_host_now_us(&host)) == expected);
        }
        else if(point == FAIL_RENDER)
            CHECK(hk_app_switch_poll(
                      hk_app_runtime_host_switch(&host),
                      hk_app_runtime_host_now_us(&host)) == expected);
        else
            CHECK(hk_app_switch_close(
                      hk_app_runtime_host_switch(&host),
                      HK_APP_STOP_FORCED) == expected);
    }
    CHECK(check_inactive(&host, expected) == 0);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == owner_calls);
    if(point == FAIL_START ||
       (!launch_fails && point != FAIL_GRANT_TIME))
        CHECK(s_simple.stop_calls == 1U);
    else if(launch_fails && point != FAIL_START)
        CHECK(s_simple.stop_calls == 0U);
    return 0;
}

static int check_capability_contracts(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_app_switch_t *switcher;
    uint8_t consumed = 0U;
    hk_input_event_t input = {0};
    uint32_t index;

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "minimal-fixture", &minimal_app_entry, sizeof(minimal_state_t));
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
    input.changed = HK_INPUT_BUTTON_OK;
    input.pressed = HK_INPUT_BUTTON_OK;
    CHECK(hk_app_switch_input(switcher, &input, &consumed) == HK_OK);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_media(
              switcher, HK_APP_MEDIA_MOUNTED, 1U,
              hk_app_runtime_host_now_us(&host)) == HK_OK);
    CHECK(hk_app_runtime_host_advance_us(&host, 500U) == HK_OK);
    CHECK(hk_app_switch_poll(
              switcher, hk_app_runtime_host_now_us(&host)) == HK_OK);
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
    CHECK(check_inactive(&host, HK_OK) == 0);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);
    return 0;
}

static int check_start_invariants(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;

    CHECK(reset_simple(&host, &app) == 0);
    s_simple.fail = FAIL_START_RENDER;
    CHECK(open_app(&host, &app, HK_ERR_INVALID_STATE) == 0);
    CHECK(check_inactive(&host, HK_ERR_INVALID_STATE) == 0);
    CHECK(s_simple.stop_calls == 1U);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);

    CHECK(reset_simple(&host, &app) == 0);
    s_simple.fail = FAIL_START_PENDING;
    CHECK(open_app(&host, &app, HK_ERR_INVALID_STATE) == 0);
    CHECK(check_inactive(&host, HK_ERR_INVALID_STATE) == 0);
    return 0;
}

static int check_tick_render_budget(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;

    CHECK(reset_simple(&host, &app) == 0);
    s_simple.fail = FAIL_SLOW_TICK;
    CHECK(open_app(&host, &app, HK_OK) == 0);
    CHECK(hk_app_runtime_host_advance_us(&host, 500U) == HK_OK);
    CHECK(hk_app_switch_poll(
              hk_app_runtime_host_switch(&host),
              hk_app_runtime_host_now_us(&host)) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(check_inactive(&host, HK_OK) == 0);
    CHECK(s_simple.stop_reason == HK_APP_STOP_DEADLINE);
    CHECK(s_simple.stop_calls == 1U);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);

    CHECK(reset_simple(&host, &app) == 0);
    s_simple.fail = FAIL_SLOW_RENDER;
    CHECK(open_app(&host, &app, HK_OK) == 0);
    CHECK(hk_app_switch_poll(
              hk_app_runtime_host_switch(&host),
              hk_app_runtime_host_now_us(&host)) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(check_inactive(&host, HK_OK) == 0);
    CHECK(s_simple.stop_reason == HK_APP_STOP_DEADLINE);
    CHECK(s_simple.stop_calls == 1U);
    return 0;
}

static int check_teardown_deadline_and_generation(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_app_runtime_t *runtime;
    hk_deadline_t expected;
    hk_app_context_t stale;
    hk_app_wakeup_token_t token;
    uint8_t consumed = 0U;
    hk_input_event_t input = {0};

    CHECK(reset_simple(&host, &app) == 0);
    CHECK(open_app(&host, &app, HK_OK) == 0);
    runtime = hk_app_runtime_host_runtime(&host);
    CHECK(hk_app_runtime_host_push_input(&host, HK_INPUT_BUTTON_OK) == HK_OK);
    input.sequence = 1U;
    input.timestamp_us = hk_app_runtime_host_now_us(&host);
    input.state = HK_INPUT_BUTTON_OK;
    input.pressed = HK_INPUT_BUTTON_OK;
    CHECK(hk_app_switch_input(
              hk_app_runtime_host_switch(&host), &input, &consumed) == HK_OK);
    token = s_simple.token;
    CHECK(hk_app_runtime_validate_wakeup_token(runtime, token) == HK_OK);
    expected.at_us = hk_app_runtime_host_now_us(&host) +
                     HK_APP_RUNTIME_HOST_TEARDOWN_BUDGET_US;
    CHECK(hk_app_switch_close(
              hk_app_runtime_host_switch(&host),
              HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(s_simple.stop_deadline.at_us == expected.at_us);
    CHECK(hk_app_runtime_host_owner_deadline(&host).at_us == expected.at_us);
    CHECK(hk_app_runtime_validate_wakeup_token(runtime, token) ==
          HK_ERR_STALE_HANDLE);
    stale = s_simple.copied;
    CHECK(hk_app_context_teardown_deadline(
              &stale, &expected) == HK_ERR_STALE_HANDLE);
    CHECK(hk_app_runtime_event(runtime, &(hk_app_event_t){
              sizeof(hk_app_event_t), HK_APP_EVENT_VERSION,
              HK_APP_EVENT_MEDIA, 0U, 1U, 0U,
              {.media = {HK_APP_MEDIA_ERROR, 1U}},
          }) == HK_ERR_INVALID_STATE);
    CHECK(check_inactive(&host, HK_OK) == 0);
    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "host-simple", &s_simple_entry, sizeof(s_simple_state));
    CHECK(open_app(&host, &app, HK_OK) == 0);
    CHECK(hk_app_switch_close(
              hk_app_runtime_host_switch(&host),
              HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

static int check_provider_quarantine(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;

    CHECK(reset_simple(&host, &app) == 0);
    s_simple.fail = FAIL_HOLD_TIME;
    hk_app_runtime_host_fail_provider_cleanup(&host, HK_ERR_IO);
    CHECK(open_app(&host, &app, HK_OK) == 0);
    CHECK(hk_app_switch_close(
              hk_app_runtime_host_switch(&host),
              HK_APP_STOP_COMPLETED) == HK_ERR_INTERNAL);
    CHECK(check_inactive(&host, HK_ERR_INTERNAL) == 0);
    CHECK(hk_app_runtime_host_time_quarantined(&host));
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 1U);
    CHECK(open_app(&host, &app, HK_ERR_INVALID_STATE) == 0);
    CHECK(hk_app_runtime_state(hk_app_runtime_host_runtime(&host)) ==
          HK_APP_RUNTIME_INACTIVE);
    CHECK(hk_app_runtime_state(hk_app_runtime_host_runtime(&host)) !=
          HK_APP_RUNTIME_FAULTED);
    return 0;
}

static int check_invalid_tick_budget(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;

    CHECK(reset_simple(&host, &app) == 0);
    app.limits.tick_budget_us = app.limits.tick_interval_us + 1U;
    CHECK(open_app(&host, &app, HK_ERR_INVALID_ARGUMENT) == 0);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) == 0U);
    return 0;
}

static int check_minimal_storage_isolation(void)
{
    hk_app_v2_entry_t entry = minimal_app_entry;
    hk_app_runtime_host_t host;
    hk_app_t app;

    entry.state_storage = s_alt_storage;
    entry.state_capacity_bytes = sizeof(s_alt_storage);
    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "minimal-alt", &entry, sizeof(minimal_state_t));
    CHECK(open_app(&host, &app, HK_OK) == 0);
    CHECK(hk_app_switch_close(
              hk_app_runtime_host_switch(&host),
              HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

int main(void)
{
    static const fail_point_t points[] = {
        FAIL_GRANT_TIME,
        FAIL_GRANT_INPUT,
        FAIL_GRANT_DISPLAY,
        FAIL_GRANT_SERVICE,
        FAIL_START,
        FAIL_EVENT,
        FAIL_TICK,
        FAIL_RENDER,
        FAIL_STOP,
        FAIL_OWNER_CLEANUP,
    };
    size_t index;

    CHECK(HK_APP_SDK_VERSION_MAJOR == 0U);
    CHECK(HK_APP_SDK_VERSION_MINOR == 2U);
    CHECK(HK_APP_SDK_RUNTIME_MAXIMUM_EXCLUSIVE_MINOR == 3U);
    CHECK(HK_APP_SDK_MANIFEST_SCHEMA_MAJOR == 1U);
    for(index = 0U; index < sizeof(points) / sizeof(points[0]); index++)
        CHECK(check_failure_point(points[index]) == 0);
    CHECK(check_capability_contracts() == 0);
    CHECK(check_start_invariants() == 0);
    CHECK(check_tick_render_budget() == 0);
    CHECK(check_teardown_deadline_and_generation() == 0);
    CHECK(check_provider_quarantine() == 0);
    CHECK(check_invalid_tick_budget() == 0);
    CHECK(check_minimal_storage_isolation() == 0);
    printf("APP_RUNTIME_HOST_OK\n");
    return 0;
}
