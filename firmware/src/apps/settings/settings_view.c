#include "settings_view.h"
#include "../../core/hk_ui_colors.h"

#include <hackylens/app/panel.h>
#include <stdio.h>
#include <string.h>

#include "settings_view_assets.h"

static void draw_value(hk_app_panel_t *panel, uint16_t y, const char *title,
                       const char *value, uint8_t selected, uint8_t editing,
                       uint8_t reserve_value)
{
    const uint16_t width = panel->width - 28U;
    uint16_t value_width = (uint16_t)strlen(value) * HACKYLENS_FONT_W;
    const uint16_t title_width = (uint16_t)strlen(title) * HACKYLENS_FONT_W;
    const uint16_t color = selected && !editing ? HK_PANEL_BLACK : hk_ui_accent();

    if(selected && !editing)
        hk_app_panel_fill(panel, 6U, y, panel->width - 12U,
                          SETTINGS_MENU_ROW_H - 3U, hk_ui_accent());
    if(!reserve_value && value_width && title_width + value_width + HACKYLENS_FONT_W > width)
        value_width = title_width + HACKYLENS_FONT_W < width ?
            (uint16_t)(width - title_width - HACKYLENS_FONT_W) : 0U;
    const uint16_t gap = value_width ? HACKYLENS_FONT_W : 0U;
    hk_app_panel_label(panel, 14U, y + 3U, width - value_width - gap,
                       title, color, HK_PANEL_LEFT);
    if(value_width)
    {
        const uint16_t x = panel->width - 14U - value_width;
        hk_app_panel_label(panel, x, y + 3U, value_width, value, color, HK_PANEL_RIGHT);
        if(editing)
            hk_app_panel_fill(panel, x - 3U, y + SETTINGS_MENU_ROW_H - 4U,
                              value_width + 6U, 2U, hk_ui_accent());
    }
}

static void draw_startup_value(hk_app_panel_t *panel, const char *value, uint8_t editing)
{
    const uint16_t width = panel->width - 28U;
    const uint16_t y = SETTINGS_MENU_ROW_Y0 + SETTINGS_MENU_ROW_H + 14U;
    hk_app_panel_label(panel, 14U + HACKYLENS_FONT_W, y, width - 2U * HACKYLENS_FONT_W,
                       value, hk_ui_accent(), HK_PANEL_CENTER);
    if(editing)
    {
        hk_app_panel_label(panel, 14U, y, HACKYLENS_FONT_W, "<", hk_ui_accent(), HK_PANEL_LEFT);
        hk_app_panel_label(panel, panel->width - 14U - HACKYLENS_FONT_W, y,
                           HACKYLENS_FONT_W, ">", hk_ui_accent(), HK_PANEL_LEFT);
    }
}

static void draw_rgb_sample(hk_app_panel_t *panel, const settings_menu_session_t *menu)
{
    uint16_t red = (uint16_t)settings_menu_display_value(menu, SETTINGS_RGB_RED);
    uint16_t green = (uint16_t)settings_menu_display_value(menu, SETTINGS_RGB_GREEN);
    uint16_t blue = (uint16_t)settings_menu_display_value(menu, SETTINGS_RGB_BLUE);
    uint16_t color = (uint16_t)(((red * 31U / 100U) << 11) |
                               ((green * 63U / 100U) << 5) | (blue * 31U / 100U));

    hk_app_panel_fill(panel, 14U, 151U, panel->width - 28U, 52U, hk_ui_accent());
    hk_app_panel_fill(panel, 16U, 153U, panel->width - 32U, 48U, color);
}

static void draw_menu_order(hk_app_panel_t *panel, const settings_order_editor_t *order)
{
    const uint16_t width = panel->width - 28U;
    const uint16_t hint_y = SETTINGS_MENU_ROW_Y0 +
                            SETTINGS_ORDER_VISIBLE_ROWS * SETTINGS_MENU_ROW_H;

    hk_app_panel_chrome(panel, "MENU ORDER");
    for(uint8_t slot = 0; slot < SETTINGS_ORDER_VISIBLE_ROWS; slot++)
    {
        uint8_t index = order->top + slot;
        if(index >= order->count)
            break;
        const hk_menu_item_t *item = &order->items[index];
        const char *title = item->app ? item->app->title : item->shortcut;
        uint8_t selected = index == order->index;
        uint8_t moving = selected && order->moving;
        char position[12];

        snprintf(position, sizeof(position), moving ? "<%u>" : "%u", index + 1U);
        draw_value(panel, SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H,
                   title ? title : "", position, selected, moving, 1U);
    }
    hk_app_panel_label(panel, 14U, hint_y, width,
                       order->moving ? "OK Place" : "OK Move",
                       hk_ui_accent(), HK_PANEL_CENTER);
}

static const char *scan_status(hk_result_t result)
{
    switch(result)
    {
    case HK_PENDING: return "SCANNING";
    case HK_ERR_BUSY: return "BUS BUSY";
    case HK_ERR_DEADLINE_EXCEEDED: return "SCL TIMEOUT";
    case HK_ERR_CAPABILITY_ABSENT:
    case HK_ERR_FEATURE_UNAVAILABLE: return "UNAVAILABLE";
    default: return "SCAN ERROR";
    }
}

static void draw_scan(hk_app_panel_t *panel, const settings_scan_t *scan)
{
    const external_link_scan_t *operation = &scan->operation;
    char heading[16], hint[22] = "OK RESCAN";
    hk_app_panel_chrome(panel, "I2C SCAN");
    if(operation->result != HK_OK || operation->count == 0U)
        hk_app_panel_status(panel, 102U, operation->result == HK_OK ?
                            "NO DEVICES" : scan_status(operation->result));
    else
    {
        snprintf(heading, sizeof(heading), "FOUND %u", (unsigned)operation->count);
        hk_app_panel_status(panel, 37U, heading);
        const uint16_t column_width = (panel->width - 12U) / SETTINGS_SCAN_COLUMNS;
        const unsigned first = scan->page * SETTINGS_SCAN_PAGE_SIZE;
        for(unsigned i = 0U; i < SETTINGS_SCAN_PAGE_SIZE && first + i < operation->count; ++i)
        {
            char address[5];
            snprintf(address, sizeof(address), "0x%02X",
                     (unsigned)external_link_scan_address(operation, first + i));
            hk_app_panel_label(panel, 6U + (i % SETTINGS_SCAN_COLUMNS) * column_width,
                              70U + (i / SETTINGS_SCAN_COLUMNS) * 25U, column_width,
                              address, hk_ui_accent(), HK_PANEL_CENTER);
        }
        uint8_t pages = settings_scan_page_count(scan);
        if(pages > 1U)
            snprintf(hint, sizeof(hint), "<%u/%u> OK RESCAN", (unsigned)scan->page + 1U,
                     (unsigned)pages);
    }
    hk_app_panel_status(panel, 212U, operation->result == HK_PENDING ? "BACK CANCEL" : hint);
}

hk_result_t settings_view_render(hk_app_surface_t *surface, const settings_state_t *state)
{
    if(!surface || !state || !state->menu.definition)
        return HK_ERR_INVALID_ARGUMENT;
    const settings_menu_session_t *menu = &state->menu;
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    if(state->page == SETTINGS_PAGE_MENU_ORDER)
    {
        draw_menu_order(&panel, &state->order);
        return panel.result;
    }
    if(state->page == SETTINGS_PAGE_I2C_SCAN)
    {
        draw_scan(&panel, &state->scan);
        return panel.result;
    }
    hk_app_panel_chrome(&panel, menu->definition->title);

    for(uint8_t slot = 0U; slot < SETTINGS_MENU_VISIBLE_ROWS; slot++)
    {
        const char *title;
        char value[SETTINGS_MENU_VALUE_SIZE];
        uint8_t selected;
        uint8_t editing;
        if(!settings_menu_visible_slot(menu, slot, &title, value, sizeof(value),
                                       &selected, &editing))
            continue;
        if(state->page == SETTINGS_PAGE_STARTUP)
        {
            draw_value(&panel, SETTINGS_MENU_ROW_Y0, title, "", selected, 0U, 0U);
            draw_startup_value(&panel, value, editing);
        }
        else
            draw_value(&panel, SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H,
                       title, value, selected, editing, 0U);
    }

    if(state->page == SETTINGS_PAGE_RGB)
        draw_rgb_sample(&panel, menu);
    return panel.result;
}
