#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
#include "../firmware/src/apps/settings/settings_app.h"
#include "../firmware/src/apps/settings/settings_menu.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

enum
{
    ITEM_TEXT = 0,
    ITEM_COUNT,
};

static const settings_menu_item_t s_items[] = {
    {
        .id = ITEM_TEXT,
        .title = "Version",
        .kind = SETTINGS_MENU_ITEM_TEXT,
    },
};

static const settings_menu_definition_t s_definition = {
    .title = "SETTINGS",
    .items = s_items,
    .item_count = (uint8_t)ITEM_COUNT,
};

const settings_menu_definition_t *settings_app_menu_definition(void)
{
    return &s_definition;
}

void settings_menu_view_open(const char *title)
{
    (void)title;
}

void settings_menu_view_clear_rows(void)
{
}

void settings_menu_view_draw_row(uint8_t slot,
                                 const char *title,
                                 const char *value,
                                 uint8_t selected,
                                 uint8_t editing)
{
    (void)slot;
    (void)title;
    (void)value;
    (void)selected;
    (void)editing;
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
        &app, "settings", &settings_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_RIGHT, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == &app);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_BACK, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == NULL);

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "settings", &settings_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_close(switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == NULL);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) >= 1U);

    printf("SETTINGS_RUNTIME_OK\n");
    return 0;
}
