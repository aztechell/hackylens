#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

#include <hackylens/app.h>

#include "settings_menu.h"
#include "../../core/hk_menu.h"
#include "../../config/settings_menu_layout.h"
#include "../../services/external_link_scan.h"

#define SETTINGS_ORDER_VISIBLE_ROWS (SETTINGS_MENU_VISIBLE_ROWS - 1U)
#define SETTINGS_SCAN_COLUMNS 3U
#define SETTINGS_SCAN_ROWS 5U
#define SETTINGS_SCAN_PAGE_SIZE (SETTINGS_SCAN_COLUMNS * SETTINGS_SCAN_ROWS)

typedef struct
{
    hk_menu_item_t items[HK_MENU_ORDER_CAPACITY];
    char shortcut_names[HK_MENU_SHORTCUT_CAPACITY][HK_MENU_ORDER_NAME_SIZE];
    uint32_t repeat_button;
    uint8_t count;
    uint8_t index;
    uint8_t top;
    uint8_t original_index;
    uint8_t moving;
    uint8_t repeat_ticks;
} settings_order_editor_t;

typedef struct
{
    external_link_scan_t operation;
    uint8_t page;
} settings_scan_t;

static inline uint8_t settings_scan_page_count(const settings_scan_t *scan)
{
    return scan->operation.count ?
        (scan->operation.count + SETTINGS_SCAN_PAGE_SIZE - 1U) / SETTINGS_SCAN_PAGE_SIZE : 1U;
}

typedef struct
{
    settings_menu_session_t menu;
    union
    {
        settings_startup_choices_t startup;
        settings_order_editor_t order;
        settings_scan_t scan;
    };
    const hk_input_t *input;
    uint8_t close_requested;
    uint8_t dirty;
    settings_page_t page;
    uint8_t root_index;
    uint8_t parent_index;
    uint8_t sd_usage_status;
    uint8_t sd_usage_progress;
    uint64_t sd_used_bytes;
    uint64_t sd_total_bytes;
} settings_state_t;

void settings_controller_reset(
    settings_state_t *state, const settings_menu_definition_t *definition);
void settings_controller_exit(settings_state_t *state);
void settings_controller_handle_input(
    settings_state_t *state, const hk_input_event_t *event);
void settings_controller_tick(settings_state_t *state, uint32_t buttons);

#endif
