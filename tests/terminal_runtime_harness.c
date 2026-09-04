#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
#include "../firmware/src/apps/terminal/terminal_app.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static uint8_t s_flags;

uint8_t settings_feature_flags(void)
{
    return s_flags;
}

void settings_set_feature_flags(uint8_t flags)
{
    s_flags = flags;
}

void settings_mark_dirty(uint8_t immediate)
{
    (void)immediate;
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

    s_flags = 0U;
    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "terminal", &terminal_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_LEFT, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_BACK, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == NULL);

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "terminal", &terminal_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_close(switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == NULL);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) >= 1U);

    printf("TERMINAL_RUNTIME_OK\n");
    return 0;
}
