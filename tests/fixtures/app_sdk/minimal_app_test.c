#include <hackylens/app/host_fake.h>

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

static const hk_app_host_fake_grant_t s_grants[] = {
    {HK_CAPABILITY_ID_TIME, 0U, 0U, 1U, NULL},
    {HK_CAPABILITY_ID_INPUT, 0U, 0U, 1U, NULL},
    {HK_CAPABILITY_ID_DISPLAY, 0U, 0U, 1U, NULL},
};

static const hk_app_host_fake_service_t s_services[] = {
    {"hackylens.service.fixture", "minimal-fixture.service"},
};

static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_start_render_storage[sizeof(minimal_state_t) + 64U];
static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_pending_storage[sizeof(minimal_state_t) + 64U];
static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_slow_tick_storage[sizeof(minimal_state_t) + 64U];
static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_slow_render_storage[sizeof(minimal_state_t) + 64U];

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

static hk_result_t slow_render(
    const hk_app_context_t *ctx,
    hk_app_surface_t *surface)
{
    (void)surface;
    return consume_callback_budget(ctx);
}

static hk_app_host_fake_config_t config(void)
{
    hk_app_host_fake_config_t value = {
        .struct_size = sizeof(value),
        .struct_version = HK_APP_HOST_FAKE_VERSION,
        .app_id = "minimal-fixture",
        .entry = &minimal_app_entry,
        .grants = s_grants,
        .grant_count = (uint16_t)(sizeof(s_grants) / sizeof(s_grants[0])),
        .services = s_services,
        .service_count =
            (uint16_t)(sizeof(s_services) / sizeof(s_services[0])),
        .state_bytes = sizeof(minimal_state_t),
        .tick_interval_us = 500U,
        .tick_budget_us = 100U,
        .render_budget_us = 100U,
        .initial_time_us = UINT64_C(1000),
        .teardown_budget_us = UINT64_C(100000),
        .display_width = 320U,
        .display_height = 240U,
    };
    return value;
}

static int check_failure_point(hk_app_host_fake_failure_point_t point)
{
    hk_app_host_fake_t fake;
    hk_app_host_fake_config_t fake_config = config();
    hk_app_host_fake_snapshot_t snapshot = {
        .struct_size = sizeof(snapshot),
        .struct_version = HK_APP_HOST_FAKE_VERSION,
    };
    hk_result_t result;

    CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
    CHECK(hk_app_host_fake_set_failure(&fake, point, HK_ERR_IO) == HK_OK);
    if(point <= HK_APP_HOST_FAKE_FAIL_START)
    {
        result = hk_app_host_fake_launch(&fake);
    }
    else
    {
        CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
        if(point == HK_APP_HOST_FAKE_FAIL_EVENT)
            result = hk_app_host_fake_media(&fake, HK_APP_MEDIA_ERROR, 9U);
        else if(point == HK_APP_HOST_FAKE_FAIL_TICK)
        {
            CHECK(hk_app_host_fake_advance_time(
                      &fake, fake_config.tick_interval_us) == HK_OK);
            result = hk_app_host_fake_tick(&fake);
        }
        else if(point == HK_APP_HOST_FAKE_FAIL_RENDER)
            result = hk_app_host_fake_render(&fake);
        else
            result = hk_app_host_fake_stop(&fake, HK_APP_STOP_FORCED);
    }
    CHECK(result == HK_ERR_IO);
    CHECK(hk_app_host_fake_snapshot(&fake, &snapshot) == HK_OK);
    CHECK(snapshot.state == HK_APP_HOST_FAKE_INACTIVE);
    CHECK(snapshot.first_error == HK_ERR_IO);
    if(point == HK_APP_HOST_FAKE_FAIL_PROBE)
    {
        CHECK(snapshot.owner_cleanup_calls == 0U);
    }
    else
    {
        CHECK(snapshot.owner_cleanup_calls == 1U);
    }
    if(point >= HK_APP_HOST_FAKE_FAIL_START)
        CHECK(snapshot.cleanup_calls == 1U ||
              point == HK_APP_HOST_FAKE_FAIL_CLEANUP);
    return 0;
}

int main(void)
{
    hk_app_host_fake_t fake;
    hk_app_host_fake_config_t fake_config = config();
    hk_app_host_fake_snapshot_t snapshot = {
        .struct_size = sizeof(snapshot),
        .struct_version = HK_APP_HOST_FAKE_VERSION,
    };

    CHECK(HK_APP_SDK_VERSION_MAJOR == 0U);
    CHECK(HK_APP_SDK_VERSION_MINOR == 1U);
    CHECK(HK_APP_SDK_RUNTIME_MAXIMUM_EXCLUSIVE_MINOR == 2U);
    CHECK(HK_APP_SDK_MANIFEST_SCHEMA_MAJOR == 1U);
    {
        hk_app_host_fake_config_t invalid_config = fake_config;

        invalid_config.tick_budget_us = invalid_config.tick_interval_us + 1U;
        CHECK(hk_app_host_fake_initialize(&fake, &invalid_config) ==
              HK_ERR_INVALID_ARGUMENT);
    }
    CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
    CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
    CHECK(hk_app_host_fake_tick(&fake) == HK_PENDING);
    CHECK(minimal_app_check_time_contract());
    CHECK(minimal_app_check_display_contract());
    CHECK(minimal_app_check_stale_reacquire());
    CHECK(hk_app_host_fake_input(&fake, HK_INPUT_BUTTON_OK) == HK_OK);
    CHECK(hk_app_host_fake_media(&fake, HK_APP_MEDIA_MOUNTED, 1U) == HK_OK);
    CHECK(hk_app_host_fake_advance_time(&fake, 500U) == HK_OK);
    CHECK(hk_app_host_fake_tick(&fake) == HK_OK);
    CHECK(hk_app_host_fake_render(&fake) == HK_OK);
    CHECK(hk_app_host_fake_render(&fake) == HK_OK);
    CHECK(hk_app_host_fake_render(&fake) == HK_PENDING);
    minimal_app_set_consume_input(0U);
    for(uint32_t index = 0U; index < 9U; index++)
    {
        uint32_t state = (index & 1U) ?
            HK_INPUT_BUTTON_OK : HK_INPUT_BUTTON_LEFT;
        CHECK(hk_app_host_fake_input(&fake, state) == HK_OK);
    }
    CHECK(minimal_app_check_input_overflow(9U));
    CHECK(hk_app_host_fake_stop(&fake, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_host_fake_snapshot(&fake, &snapshot) == HK_OK);
    CHECK(snapshot.state == HK_APP_HOST_FAKE_INACTIVE);
    CHECK(snapshot.first_error == HK_OK);
    CHECK(snapshot.probe_calls == 1U && snapshot.prepare_calls == 1U);
    CHECK(snapshot.start_calls == 1U && snapshot.stop_calls == 1U);
    CHECK(snapshot.cleanup_calls == 1U && snapshot.owner_cleanup_calls == 1U);
    CHECK(snapshot.event_calls == 13U && snapshot.tick_calls == 1U);
    CHECK(snapshot.render_calls == 2U && snapshot.display_operations == 5U);
    CHECK(snapshot.display_present_calls == 2U);

    fake_config = config();
    CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
    CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
    CHECK(hk_app_host_fake_set_failure(
              &fake, HK_APP_HOST_FAKE_FAIL_TICK, HK_ERR_IO) == HK_OK);
    CHECK(hk_app_host_fake_advance_time(
              &fake, fake_config.tick_interval_us) == HK_OK);
    CHECK(hk_app_host_fake_tick(&fake) == HK_ERR_IO);
    snapshot = (hk_app_host_fake_snapshot_t){
        .struct_size = sizeof(snapshot),
        .struct_version = HK_APP_HOST_FAKE_VERSION,
    };
    CHECK(hk_app_host_fake_snapshot(&fake, &snapshot) == HK_OK);
    CHECK(snapshot.state == HK_APP_HOST_FAKE_INACTIVE);
    CHECK(snapshot.first_error == HK_ERR_IO);
    CHECK(snapshot.stop_calls == 1U && snapshot.cleanup_calls == 1U);
    CHECK(snapshot.owner_cleanup_calls == 1U);
    CHECK(snapshot.event_calls == 2U);

    {
        hk_app_v2_entry_t entry = minimal_app_entry;

        entry.state_storage = s_start_render_storage;
        entry.state_capacity_bytes = sizeof(s_start_render_storage);
        entry.start = start_requests_render;
        fake_config = config();
        fake_config.entry = &entry;
        CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
        CHECK(hk_app_host_fake_launch(&fake) == HK_ERR_INVALID_STATE);
        snapshot = (hk_app_host_fake_snapshot_t){
            .struct_size = sizeof(snapshot),
            .struct_version = HK_APP_HOST_FAKE_VERSION,
        };
        CHECK(hk_app_host_fake_snapshot(&fake, &snapshot) == HK_OK);
        CHECK(snapshot.first_error == HK_ERR_INVALID_STATE);
        CHECK(snapshot.stop_calls == 1U && snapshot.cleanup_calls == 1U);
        CHECK(snapshot.owner_cleanup_calls == 1U);
    }

    {
        hk_app_v2_entry_t entry = minimal_app_entry;

        entry.state_storage = s_pending_storage;
        entry.state_capacity_bytes = sizeof(s_pending_storage);
        entry.start = start_returns_pending;
        fake_config = config();
        fake_config.entry = &entry;
        CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
        CHECK(hk_app_host_fake_launch(&fake) == HK_ERR_INVALID_STATE);
        snapshot = (hk_app_host_fake_snapshot_t){
            .struct_size = sizeof(snapshot),
            .struct_version = HK_APP_HOST_FAKE_VERSION,
        };
        CHECK(hk_app_host_fake_snapshot(&fake, &snapshot) == HK_OK);
        CHECK(snapshot.first_error == HK_ERR_INVALID_STATE);
    }

    {
        hk_app_v2_entry_t entry = minimal_app_entry;

        entry.state_storage = s_slow_tick_storage;
        entry.state_capacity_bytes = sizeof(s_slow_tick_storage);
        entry.tick = slow_tick;
        fake_config = config();
        fake_config.entry = &entry;
        CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
        CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
        CHECK(hk_app_host_fake_advance_time(
                  &fake, fake_config.tick_interval_us) == HK_OK);
        CHECK(hk_app_host_fake_tick(&fake) == HK_ERR_DEADLINE_EXCEEDED);
    }

    {
        hk_app_v2_entry_t entry = minimal_app_entry;

        entry.state_storage = s_slow_render_storage;
        entry.state_capacity_bytes = sizeof(s_slow_render_storage);
        entry.render = slow_render;
        fake_config = config();
        fake_config.entry = &entry;
        CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
        CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
        CHECK(hk_app_host_fake_render(&fake) == HK_ERR_DEADLINE_EXCEEDED);
    }

    {
        static const hk_app_host_fake_failure_point_t points[] = {
            HK_APP_HOST_FAKE_FAIL_PROBE,
            HK_APP_HOST_FAKE_FAIL_GRANT_TIME,
            HK_APP_HOST_FAKE_FAIL_GRANT_INPUT,
            HK_APP_HOST_FAKE_FAIL_GRANT_DISPLAY,
            HK_APP_HOST_FAKE_FAIL_GRANT_SERVICE,
            HK_APP_HOST_FAKE_FAIL_PREPARE,
            HK_APP_HOST_FAKE_FAIL_START,
            HK_APP_HOST_FAKE_FAIL_EVENT,
            HK_APP_HOST_FAKE_FAIL_TICK,
            HK_APP_HOST_FAKE_FAIL_RENDER,
            HK_APP_HOST_FAKE_FAIL_STOP,
            HK_APP_HOST_FAKE_FAIL_CLEANUP,
            HK_APP_HOST_FAKE_FAIL_OWNER_CLEANUP,
        };
        size_t index;

        for(index = 0U; index < sizeof(points) / sizeof(points[0]); index++)
            CHECK(check_failure_point(points[index]) == 0);
    }
    printf("APP_SDK_FIXTURE_OK\n");
    return 0;
}
