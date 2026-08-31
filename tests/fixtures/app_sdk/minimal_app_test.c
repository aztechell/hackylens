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
            result = hk_app_host_fake_tick(&fake);
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
    CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
    CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
    CHECK(hk_app_host_fake_input(&fake, HK_INPUT_BUTTON_OK) == HK_OK);
    CHECK(hk_app_host_fake_media(&fake, HK_APP_MEDIA_MOUNTED, 1U) == HK_OK);
    CHECK(hk_app_host_fake_advance_time(&fake, 500U) == HK_OK);
    CHECK(hk_app_host_fake_tick(&fake) == HK_OK);
    CHECK(hk_app_host_fake_render(&fake) == HK_OK);
    CHECK(hk_app_host_fake_render(&fake) == HK_OK);
    CHECK(hk_app_host_fake_render(&fake) == HK_PENDING);
    CHECK(hk_app_host_fake_stop(&fake, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_host_fake_snapshot(&fake, &snapshot) == HK_OK);
    CHECK(snapshot.state == HK_APP_HOST_FAKE_INACTIVE);
    CHECK(snapshot.first_error == HK_OK);
    CHECK(snapshot.probe_calls == 1U && snapshot.prepare_calls == 1U);
    CHECK(snapshot.start_calls == 1U && snapshot.stop_calls == 1U);
    CHECK(snapshot.cleanup_calls == 1U && snapshot.owner_cleanup_calls == 1U);
    CHECK(snapshot.event_calls == 4U && snapshot.tick_calls == 1U);
    CHECK(snapshot.render_calls == 2U && snapshot.display_operations == 4U);
    CHECK(snapshot.display_present_calls == 2U);

    fake_config = config();
    CHECK(hk_app_host_fake_initialize(&fake, &fake_config) == HK_OK);
    CHECK(hk_app_host_fake_launch(&fake) == HK_OK);
    CHECK(hk_app_host_fake_set_failure(
              &fake, HK_APP_HOST_FAKE_FAIL_TICK, HK_ERR_IO) == HK_OK);
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
