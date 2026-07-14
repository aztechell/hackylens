#include "qr_result_view.h"

#include <stdio.h>

#include "../config/display_config.h"
#include "../config/menu_layout.h"
#include "../config/qr_layout.h"

#include "../drivers/hk_lcd.h"

void qr_result_view_draw_status(const char *status)
{
    lcd_fill_rect(0, QR_RESULT_STATUS_Y, LCD_W, HACKYLENS_FONT_H, COLOR_BLACK);
    if(status && status[0])
        lcd_draw_text_centered(QR_RESULT_STATUS_Y, status, COLOR_TERM_GREEN, COLOR_BLACK);
}

static void qr_result_view_draw_actions(void)
{
    lcd_fill_rect(0, QR_RESULT_ACTION_Y, LCD_W, HACKYLENS_FONT_H, COLOR_BLACK);
    lcd_draw_text_at(22, QR_RESULT_ACTION_Y, "OK:SAVE", COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_at(166, QR_RESULT_ACTION_Y, "BACK:CLOSE", COLOR_TERM_GREEN, COLOR_BLACK);
}

static void qr_result_view_draw_page_hint(uint16_t scroll_line, uint16_t max_scroll)
{
    if(max_scroll > 0)
    {
        char line[16];
        snprintf(line, sizeof(line), "< %u/%u >", (unsigned)(scroll_line + 1U), (unsigned)(max_scroll + 1U));
        qr_result_view_draw_status(line);
    }
    else
    {
        qr_result_view_draw_status("");
    }
}

void qr_result_view_render(const char *payload, uint16_t scroll_line, uint16_t max_scroll)
{
    if(!payload)
        payload = "";
    if(scroll_line > max_scroll)
        scroll_line = max_scroll;

    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
    lcd_draw_text_centered(4, "QR RESULT", COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_rect(QR_RESULT_FRAME_X, QR_RESULT_FRAME_Y, QR_RESULT_FRAME_W, QR_RESULT_FRAME_H, MENU_LINE, COLOR_TERM_GREEN);

    if(!payload[0])
    {
        lcd_draw_text_centered(QR_RESULT_TEXT_Y + HACKYLENS_FONT_H, "EMPTY PAYLOAD", COLOR_TERM_GREEN, COLOR_BLACK);
    }
    else
    {
        for(uint16_t row = 0; row < QR_RESULT_TEXT_ROWS; row++)
        {
            uint16_t line_index = (uint16_t)(scroll_line + row);
            uint32_t offset = (uint32_t)line_index * QR_RESULT_TEXT_COLS;
            char line[QR_RESULT_TEXT_COLS + 1U];
            uint8_t len = 0;

            if(!payload[offset])
                break;

            while(len < QR_RESULT_TEXT_COLS && payload[offset + len])
            {
                line[len] = payload[offset + len];
                len++;
            }
            line[len] = '\0';
            lcd_draw_text_at(QR_RESULT_TEXT_X,
                             (uint16_t)(QR_RESULT_TEXT_Y + row * HACKYLENS_FONT_H),
                             line,
                             COLOR_TERM_GREEN,
                             COLOR_BLACK);
        }
    }

    qr_result_view_draw_page_hint(scroll_line, max_scroll);
    qr_result_view_draw_actions();
}

void qr_result_view_clear(void)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
}
