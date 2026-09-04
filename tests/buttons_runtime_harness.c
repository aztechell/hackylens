#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
#include "../firmware/src/apps/buttons/buttons_app.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

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

static int hold_ticks(hk_app_runtime_host_t *host)
{
    unsigned tick;

    for(tick = 0U; tick < 50U; tick++)
    {
        CHECK(hk_app_runtime_host_advance_us(host, 500U) == HK_OK);
        CHECK(hk_app_switch_poll(
                  hk_app_runtime_host_switch(host),
                  hk_app_runtime_host_now_us(host)) == HK_OK);
    }
    return 0;
}

int main(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_app_switch_t *switcher;
    uint8_t consumed = 0U;
    hk_input_event_t input = {0};

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "buttons", &buttons_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_BACK, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_LEFT, &consumed) == 0);
    CHECK(hk_app_switch_active(switcher) == &app);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_OK, &consumed) == 0);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_RIGHT, &consumed) == 0);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(hk_app_switch_close(switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == NULL);

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "buttons", &buttons_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_OK, &consumed) == 0);
    CHECK(hk_app_runtime_host_push_input(
              &host, HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK) == HK_OK);
    input.sequence = 2U;
    input.timestamp_us = hk_app_runtime_host_now_us(&host);
    input.state = HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK;
    input.changed = HK_INPUT_BUTTON_BACK;
    input.pressed = HK_INPUT_BUTTON_BACK;
    CHECK(hk_app_switch_input(switcher, &input, &consumed) == HK_OK);
    CHECK(hold_ticks(&host) == 0);
    CHECK(hk_app_switch_active(switcher) == &app);
    CHECK(hk_app_runtime_host_push_input(&host, HK_INPUT_BUTTON_BACK) == HK_OK);
    input.sequence = 3U;
    input.timestamp_us = hk_app_runtime_host_now_us(&host);
    input.state = HK_INPUT_BUTTON_BACK;
    input.changed = HK_INPUT_BUTTON_OK;
    input.pressed = 0U;
    CHECK(hk_app_switch_input(switcher, &input, &consumed) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == &app);
    CHECK(hk_app_runtime_host_push_input(&host, 0U) == HK_OK);
    input.sequence = 4U;
    input.timestamp_us = hk_app_runtime_host_now_us(&host);
    input.state = 0U;
    input.changed = HK_INPUT_BUTTON_BACK;
    input.pressed = 0U;
    CHECK(hk_app_switch_input(switcher, &input, &consumed) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == NULL);

    printf("BUTTONS_RUNTIME_OK\n");
    return 0;
}
