#include "settings_view.h"

#include <string.h>

#include "settings_view_assets.h"

static hk_result_t fill_rect(
    hk_app_surface_t *surface,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    uint16_t rgb565)
{
    hk_display_rect_t rect = {x, y, width, height};

    return hk_app_surface_fill_rect(surface, &rect, rgb565);
}

static hk_result_t draw_text(
    hk_app_surface_t *surface,
    int32_t x,
    int32_t y,
    uint32_t width,
    const char *text,
    uint16_t rgb565)
{
    hk_display_rect_t bounds = {x, y, width, HACKYLENS_FONT_H};

    return hk_app_surface_text(
        surface, &bounds, text, (uint32_t)strlen(text), rgb565);
}

static hk_result_t draw_chrome(
    hk_app_surface_t *surface, const hk_display_info_t *info, const char *title)
{
    uint32_t title_px;
    int32_t title_x;
    hk_result_t result;

    result = fill_rect(surface, 0, 0, info->width, MENU_LINE, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    result = fill_rect(
        surface, 0, (int32_t)info->height - (int32_t)MENU_LINE,
        info->width, MENU_LINE, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    result = fill_rect(
        surface, 0, 0, MENU_LINE, info->height, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    result = fill_rect(
        surface, (int32_t)info->width - (int32_t)MENU_LINE, 0,
        MENU_LINE, info->height, COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    result = fill_rect(
        surface, 0, MENU_BAR_H - MENU_LINE, info->width, MENU_LINE,
        COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    result = draw_text(
        surface, 4, 3, 3U * HACKYLENS_FONT_W, "<o>", COLOR_TERM_GREEN);
    if(result != HK_OK)
        return result;
    title_px = (uint32_t)strlen(title) * HACKYLENS_FONT_W;
    title_x = title_px < info->width ?
        (int32_t)((info->width - title_px) / 2U) : 0;
    if(title_px == 0U)
        title_px = HACKYLENS_FONT_W;
    return draw_text(
        surface, title_x, 3, title_px, title, COLOR_TERM_GREEN);
}

hk_result_t settings_view_render(
    hk_app_surface_t *surface, const settings_menu_session_t *session)
{
    hk_display_info_t info = {0};
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
    if(session->definition && session->definition->title)
        title = session->definition->title;
    result = draw_chrome(surface, &info, title);
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
        uint32_t title_width;
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
        title_width = value_x > 12 ? (uint32_t)(value_x - 12) : 0U;
        result = draw_text(
            surface, 10, y + 3, title_width,
            row_title ? row_title : "", fg);
        if(result != HK_OK)
            return result;
        if(editing)
        {
            result = draw_text(
                surface, value_x, y + 3, HACKYLENS_FONT_W, "*", fg);
            if(result != HK_OK)
                return result;
            value_x += HACKYLENS_FONT_W;
        }
        result = draw_text(
            surface, value_x, y + 3,
            info.width > (uint32_t)value_x ?
                info.width - (uint32_t)value_x : 0U,
            value, fg);
        if(result != HK_OK)
            return result;
    }
    return HK_OK;
}
