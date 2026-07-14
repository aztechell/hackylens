#include "camera_settings_view.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/camera_layout.h"
#include "hk_ui.h"
#include "../drivers/hk_lcd.h"

void camera_settings_view_draw_row(const camera_settings_view_model_t *model, uint8_t index)
{
    const camera_settings_view_row_t *row;
    uint8_t visible_index;
    uint16_t y;
    uint8_t selected;
    uint16_t fg;
    uint16_t bg;

    if(!model || !model->rows || index >= model->row_count)
        return;
    if(index < model->top || index >= model->top + CAMERA_SETTINGS_VISIBLE_ROWS)
        return;

    row = &model->rows[index];
    selected = index == model->index;
    fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
    visible_index = (uint8_t)(index - model->top);
    y = CAMERA_SETTINGS_ROW_Y0 + visible_index * CAMERA_SETTINGS_ROW_H;

    lcd_fill_rect(6, y, LCD_W - 12, CAMERA_SETTINGS_ROW_H - 2, bg);
    lcd_draw_text_at(10, y + 3, row->title, fg, bg);
    lcd_draw_text_at(LCD_W - 10 - (uint16_t)(strlen(row->value) * HACKYLENS_FONT_W), y + 3, row->value, fg, bg);
}

void camera_settings_view_render(const camera_settings_view_model_t *model)
{
    if(!model)
        return;

    menu_draw_chrome(model->title);
    for(uint8_t i = 0; i < CAMERA_SETTINGS_VISIBLE_ROWS; i++)
    {
        uint8_t index = (uint8_t)(model->top + i);
        if(index < model->row_count)
            camera_settings_view_draw_row(model, index);
    }
}
