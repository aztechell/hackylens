#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
#include "../firmware/src/apps/settings/settings_app.h"
#include "../firmware/src/apps/settings/settings_menu.h"
#include "../firmware/src/core/hk_menu.h"
#include "../firmware/src/storage/fat32_volume.h"

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
    ITEM_MENU_ORDER,
    ITEM_COUNT,
};

static const hk_app_t s_camera = {.id = "camera", .title = "CAMERA"};
static const hk_app_t s_files = {.id = "files", .title = "FILES"};
static const hk_app_t *s_order_first;

uint8_t hk_menu_get_items(hk_menu_item_t items[HK_MENU_ORDER_CAPACITY])
{
    items[0] = (hk_menu_item_t){.app = &s_camera};
    items[1] = (hk_menu_item_t){.app = &s_files};
    return 2U;
}
void hk_menu_save_order(const hk_menu_item_t *items, uint8_t count)
{
    if(count == 2U)
        s_order_first = items[0].app;
}

static const settings_menu_item_t s_items[] = {
    {
        .id = ITEM_TEXT,
        .title = "Version",
        .kind = SETTINGS_MENU_ITEM_TEXT,
    },
    [ITEM_MENU_ORDER] = {
        .id = SETTINGS_PAGE_MENU_ORDER,
        .title = "Menu Order",
        .kind = SETTINGS_MENU_ITEM_ACTION,
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

const settings_menu_definition_t *settings_app_page_definition(settings_page_t page)
{
    (void)page;
    return &s_definition;
}

const settings_menu_definition_t *settings_app_startup_choices(settings_startup_choices_t *choices)
{
    (void)choices;
    return &s_definition;
}

uint8_t topbar_sd_mounted(void)
{
    return 1U;
}

void sd_service_watch_usage(uint8_t watch)
{
    (void)watch;
}

fat32_usage_t fat32_usage(void)
{
    return (fat32_usage_t){0};
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

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_OK, &consumed) == 0);
    CHECK(hk_app_switch_poll(switcher, hk_app_runtime_host_now_us(&host)) == HK_OK);
    CHECK(host.present_calls == 1U);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_OK, &consumed) == 0);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_RIGHT, &consumed) == 0);
    CHECK(hk_app_switch_poll(switcher, hk_app_runtime_host_now_us(&host)) == HK_OK);
    CHECK(host.present_calls == 2U);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_OK, &consumed) == 0);
    CHECK(s_order_first == &s_files);
    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_BACK, &consumed) == 0);
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
    CHECK(hk_app_runtime_host_cleanup_calls(&host) >= 1U);

    printf("SETTINGS_RUNTIME_OK\n");
    return 0;
}
