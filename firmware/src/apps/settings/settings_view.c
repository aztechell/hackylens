#include "settings_view.h"

#include <string.h>

#include "settings_view_assets.h"

static hk_result_t draw_text(
    hk_app_surface_t *surface,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    const char *text,
    uint16_t rgb565)
{
    hk_display_rect_t bounds = {x, y, width, height};

    return hk_app_surface_text(
        surface, &bounds, text, (uint32_t)strlen(text), rgb565);
}

hk_result_t settings_view_render(
    hk_app_surface_t *surface, const settings_menu_session_t *session)
{
    hk_display_info_t info = {0};
    hk_display_rect_t bar;
    hk_display_rect_t rule;
    hk_display_rect_t row;
    const char *title = "SETTINGS";
    uint8_t slot;
    hk_result_t result;

    if(!surface || !session)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_surface_get_info(surface, &info);
    if(result != HK_OK)
        return result;
    result = hk_app_surface_clear(surface, COLOR_BLACK);
    if(result != HK_OK)
        return result;
    bar = (hk_display_rect_t){0, 0, info.width, info.height};
    result = hk_app_surface_stroke_rect(surface, &bar, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    rule = (hk_display_rect_t){
        0, MENU_BAR_H - MENU_LINE, info.width, MENU_LINE,
    };
    result = hk_app_surface_fill_rect(surface, &rule, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    if(session->definition && session->definition->title)
        title = session->definition->title;
    result = draw_text(
        surface, 12, 6, info.width > 24U ? info.width - 24U : info.width,
        16U, title, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    for(slot = 0U; slot < SETTINGS_MENU_VISIBLE_ROWS; slot++)
    {
        const char *row_title = "";
        char value[SETTINGS_MENU_VALUE_SIZE];
        uint8_t selected = 0U;
        uint8_t editing = 0U;
        uint16_t fg;
        uint16_t bg;
        int32_t y;
        int32_t value_x;
        uint32_t row_width;
        size_t value_length;

        if(!settings_menu_visible_slot(
               session, slot, &row_title, value, sizeof(value),
               &selected, &editing))
            continue;
        fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
        bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
        y = SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H;
        row_width = info.width > 12U ? info.width - 12U : info.width;
        row = (hk_display_rect_t){
            6, y, row_width, SETTINGS_MENU_ROW_H - 2,
        };
        result = hk_app_surface_fill_rect(surface, &row, bg);
        if(result != HK_OK)
            return result;
        value_length = strlen(value);
        value_x = (int32_t)info.width - 10 -
                  (int32_t)(value_length * HACKYLENS_FONT_W);
        if(editing)
            value_x -= HACKYLENS_FONT_W;
        if(value_x < 10)
            value_x = 10;
        result = draw_text(
            surface, 10, y + 3,
            value_x > 12 ? (uint32_t)(value_x - 12) : 0U, 16U,
            row_title ? row_title : "", fg);
        if(result != HK_OK)
            return result;
        if(editing)
        {
            result = draw_text(
                surface, value_x, y + 3, HACKYLENS_FONT_W, 16U, "*", fg);
            if(result != HK_OK)
                return result;
            value_x += HACKYLENS_FONT_W;
        }
        result = draw_text(
            surface, value_x, y + 3,
            info.width > (uint32_t)value_x ?
                info.width - (uint32_t)value_x : 0U,
            16U, value, fg);
        if(result != HK_OK)
            return result;
    }
    return HK_OK;
}
