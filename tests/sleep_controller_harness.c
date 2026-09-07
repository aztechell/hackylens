#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/capability/time.h>

#include "../firmware/src/controllers/auto_sleep_controller.h"
#include "../firmware/src/core/hk_menu.h"
#include "../firmware/src/core/hk_screen.h"

static screen_t g_screen;
static uint64_t g_last_activity;
static uint64_t g_now;
static hk_result_t g_binding_result;
static hk_result_t g_now_result;
static uint32_t g_binding_count;
static uint32_t g_sleep_count;

static const hk_app_t s_sleep_app = {
    .id = "sleep",
    .title = "SLEEP",
};
const hk_app_t *const g_menu_items[] = { &s_sleep_app };
const uint8_t g_menu_item_count = 1U;

static int check(uint8_t condition, const char *message)
{
    if(condition)
        return 0;
    fprintf(stderr, "sleep controller check failed: %s\n", message);
    return 1;
}

static void reset_fixture(void)
{
    g_screen = SCREEN_MENU;
    g_last_activity = 1000000U;
    g_now = g_last_activity;
    g_binding_result = HK_OK;
    g_now_result = HK_OK;
    g_binding_count = 0U;
    g_sleep_count = 0U;
    sleep_session_set_active(0U);
}

hk_owner_t capability_client_consumer_owner(const char *consumer_id)
{
    (void)consumer_id;
    return (hk_owner_t){1U, 1U};
}

struct hk_time { uint8_t binding; };
static const hk_time_t s_time = {1U};
const hk_time_t *hk_time_service(void)
{
    g_binding_count++;
    return g_binding_result == HK_OK ? &s_time : NULL;
}

hk_result_t hk_time_now_us(
    const hk_time_t *handle,
    uint64_t *value)
{
    if(!handle) return HK_ERR_CAPABILITY_ABSENT;
    if(g_now_result == HK_OK)
        *value = g_now;
    return g_now_result;
}

screen_t hk_screen_get(void)
{
    return g_screen;
}

void hk_screen_set(screen_t screen)
{
    g_screen = screen;
}

uint8_t shell_open_app(const hk_app_t *app, const hk_input_snapshot_t *input)
{
    (void)input;
    if(!app || !app->id || strcmp(app->id, "sleep") != 0)
        return 0U;
    g_screen = SCREEN_APP;
    g_sleep_count++;
    return 1U;
}

uint64_t hk_last_activity_us(void)
{
    return g_last_activity;
}

uint8_t hk_auto_sleep_minutes(void)
{
    return 1U;
}

int main(void)
{
    hk_input_snapshot_t idle = {0U, 0U, 0U};
    hk_input_snapshot_t pressed = {1U, 1U, 1U};
    int failed = 0;

    reset_fixture();
    g_binding_result = HK_ERR_FEATURE_UNAVAILABLE;
    auto_sleep_controller_tick(&idle);
    failed |= check(g_binding_count == 1U, "Time binding must be queried once");
    failed |= check(
        g_sleep_count == 0U,
        "Absent Time binding must not force sleep");

    reset_fixture();
    g_now_result = HK_ERR_INTERNAL;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 0U,
        "TIME read failure must not force sleep");

    reset_fixture();
    auto_sleep_controller_tick(&idle);
    failed |= check(g_sleep_count == 0U, "fresh activity must stay awake");

    reset_fixture();
    g_now = g_last_activity - 1U;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 0U,
        "backward time must not underflow into sleep");

    reset_fixture();
    g_now = g_last_activity + 60000000U;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 1U && g_screen == SCREEN_APP,
        "exact inactivity deadline must enter sleep");

    reset_fixture();
    g_now = g_last_activity + 60000000U;
    auto_sleep_controller_tick(&pressed);
    failed |= check(
        g_sleep_count == 0U,
        "held input must suppress auto sleep");

    reset_fixture();
    g_screen = SCREEN_APP;
    g_now = g_last_activity + 60000000U;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 0U,
        "inactive sleep must not receive hidden auto-sleep while another screen is open");

    if(failed)
        return 1;
    puts("SLEEP_CONTROLLER_OK monotonic_only=1 failure_safe=1 wrap_safe=1");
    return 0;
}
