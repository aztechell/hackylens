#include "auto_sleep_controller.h"

#include <stdio.h>
#include <string.h>

#include <hackylens/capability/time.h>

#include "../core/hk_app_registry.h"
#include "../core/hk_capability_client.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../services/settings_service.h"

static uint8_t s_sleep_session_active;
static hk_time_t s_sleep_time;
static hk_owner_t s_sleep_time_owner;

void sleep_session_set_active(uint8_t active)
{
    s_sleep_session_active = active ? 1U : 0U;
}

uint8_t sleep_session_active(void)
{
    return s_sleep_session_active;
}

static hk_result_t sleep_time_now_us(uint64_t *value)
{
    hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;
    hk_owner_t owner = capability_client_consumer_owner(
        "consumer:firmware-runtime");

    if(!value)
        return HK_ERR_INVALID_ARGUMENT;
    *value = 0U;
    if(hk_owner_is_zero(owner))
        return HK_ERR_STALE_HANDLE;
    request.required_features = HK_TIME_FEATURE_MONOTONIC_US;
    if(owner.slot != s_sleep_time_owner.slot ||
       owner.generation != s_sleep_time_owner.generation ||
       hk_lease_is_zero(&s_sleep_time.lease))
    {
        s_sleep_time.lease = HK_LEASE_NONE;
        s_sleep_time_owner = owner;
        hk_result_t result = hk_time_acquire(
            owner, &request, &s_sleep_time);
        if(result != HK_OK)
            return result;
    }
    return hk_time_now_us(owner, &s_sleep_time, value);
}

static const hk_app_t *sleep_find_app(void)
{
    uint8_t index;

    for(index = 0U; index < g_menu_item_count; index++)
    {
        const hk_app_t *app = g_menu_items[index];

        if(app && app->id && strcmp(app->id, "sleep") == 0)
            return app;
    }
    return NULL;
}

void auto_sleep_controller_tick(const hk_input_snapshot_t *input)
{
    uint64_t now;
    uint64_t last_activity;
    uint64_t timeout_us;
    uint8_t auto_sleep_minutes = hk_auto_sleep_minutes();
    const hk_app_t *app;

    if(hk_screen_get() != SCREEN_MENU || !input || input->state ||
       auto_sleep_minutes == 0)
        return;

    last_activity = hk_last_activity_us();
    if(last_activity == 0U ||
       sleep_time_now_us(&now) != HK_OK ||
       now < last_activity)
        return;
    timeout_us = (uint64_t)auto_sleep_minutes * 60ULL * 1000000ULL;
    if(now - last_activity < timeout_us)
        return;
    app = sleep_find_app();
    if(!app)
        return;
    printf("[SLEEP] auto after %u min\r\n", auto_sleep_minutes);
    (void)shell_open_app(app, input);
}
