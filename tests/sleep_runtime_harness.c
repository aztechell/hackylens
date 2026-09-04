#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
#include "../firmware/src/apps/sleep/sleep_app.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static uint8_t s_session_active;
static uint32_t s_brightness_off;
static uint32_t s_brightness_apply;
static uint32_t s_illum_apply;
static uint32_t s_rgb_apply;

void sleep_session_set_active(uint8_t active)
{
    s_session_active = active ? 1U : 0U;
}

uint8_t sleep_session_active(void)
{
    return s_session_active;
}

void screen_brightness_off(void)
{
    s_brightness_off++;
}

void screen_brightness_apply(void)
{
    s_brightness_apply++;
}

void illum_led_apply(void)
{
    s_illum_apply++;
}

void rgb_led_apply(void)
{
    s_rgb_apply++;
}

static int dispatch_input(
    hk_app_runtime_host_t *host, uint32_t state, uint8_t *consumed)
{
    hk_input_event_t input = {0};

    CHECK(hk_app_runtime_host_push_input(host, state) == HK_OK);
    input.sequence = 1U;
    input.timestamp_us = hk_app_runtime_host_now_us(host);
    input.state = state;
    input.changed = state;
    input.pressed = state;
    CHECK(hk_app_switch_input(
              hk_app_runtime_host_switch(host), &input, consumed) == HK_OK);
    return 0;
}

int main(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_app_switch_t *switcher;
    uint8_t consumed = 0U;

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "sleep", &sleep_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == &app);
    CHECK(sleep_session_active() == 1U);
    CHECK(s_brightness_off == 1U);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_BACK, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == NULL);
    CHECK(sleep_session_active() == 0U);
    CHECK(s_brightness_apply == 1U);
    CHECK(s_illum_apply == 1U);
    CHECK(s_rgb_apply == 1U);

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    s_brightness_off = 0U;
    s_brightness_apply = 0U;
    s_illum_apply = 0U;
    s_rgb_apply = 0U;
    hk_app_runtime_host_fill_app(
        &app, "sleep", &sleep_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_close(switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == NULL);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) >= 1U);
    CHECK(sleep_session_active() == 0U);

    printf("SLEEP_RUNTIME_OK\n");
    return 0;
}
