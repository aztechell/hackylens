#include "settings_view.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/settings_config.h"
#include "../config/settings_layout.h"
#include "hk_ui.h"
#include "../drivers/hk_lcd.h"

const char *settings_view_row_title(uint8_t index)
{
    if(index == SETTINGS_LED_SWITCH)
        return "LED Switch";
    if(index == SETTINGS_LED_BRIGHTNESS)
        return "LED Bright";
    if(index == SETTINGS_RGB_SWITCH)
        return "RGB Switch";
    if(index == SETTINGS_RGB_RED)
        return "RGB Red";
    if(index == SETTINGS_RGB_GREEN)
        return "RGB Green";
    if(index == SETTINGS_RGB_BLUE)
        return "RGB Blue";
    if(index == SETTINGS_SCREEN_BRIGHTNESS)
        return "Screen Bright";
    if(index == SETTINGS_AUTO_SLEEP)
        return "Auto Sleep";
    return "Version";
}

void settings_view_draw_row(const settings_view_model_t *model, uint8_t index)
{
    const settings_view_row_t *row;
    uint8_t visible_index;
    uint16_t y;
    uint8_t selected;
    uint16_t fg;
    uint16_t bg;

    if(!model || !model->rows || index >= model->row_count)
        return;
    if(index < model->top || index >= model->top + SETTINGS_VISIBLE_ROWS)
        return;

    row = &model->rows[index];
    selected = index == model->index;
    fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
    visible_index = (uint8_t)(index - model->top);
    y = SETTINGS_ROW_Y0 + visible_index * SETTINGS_ROW_H;

    lcd_fill_rect(6, y, LCD_W - 12, SETTINGS_ROW_H - 2, bg);
    lcd_draw_text_at(10, y + 3, row->title, fg, bg);
    lcd_draw_text_at(LCD_W - 10 - (uint16_t)(strlen(row->value) * HACKYLENS_FONT_W), y + 3, row->value, fg, bg);
}

void settings_view_render(const settings_view_model_t *model)
{
    if(!model)
        return;

    menu_draw_chrome("SETTINGS");
    for(uint8_t i = 0; i < SETTINGS_VISIBLE_ROWS; i++)
    {
        uint8_t index = (uint8_t)(model->top + i);
        if(index < model->row_count)
            settings_view_draw_row(model, index);
    }
}
