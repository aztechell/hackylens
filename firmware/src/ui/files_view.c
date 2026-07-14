#include "hk_ui.h"

#include <stdio.h>
#include <string.h>

#include "../core/file_list_item.h"

#include "../config/display_config.h"
#include "../config/files_layout.h"

#include "../drivers/hk_lcd.h"
#include "../core/files_view_port.h"

static void files_view_enter_impl(void)
{
}

static void files_view_draw_status_impl(const char *line)
{
    menu_draw_chrome("FILES");
    lcd_draw_text_centered(96, line, COLOR_TERM_GREEN, COLOR_BLACK);
}

static void files_view_draw_row_impl(uint8_t row, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    uint8_t entry_index = (uint8_t)(top + row);
    uint16_t y = FILES_ROW_Y0 + row * FILES_ROW_H;
    uint8_t selected = entry_index == index;
    uint16_t fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    uint16_t bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
    char line[32];

    lcd_fill_rect(6, y, LCD_W - 12, FILES_ROW_H - 2, bg);
    if(entry_index >= count)
        return;

    snprintf(line, sizeof(line), "%c %-18.18s", items[entry_index].kind == FILE_ITEM_DIRECTORY ? 'D' : 'F', items[entry_index].name);
    lcd_draw_text_at(10, y + 1, line, fg, bg);
}

static void files_view_render_list_impl(uint8_t sd_present, uint8_t fat_mounted, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    menu_draw_chrome("FILES");
    if(!sd_present)
    {
        lcd_draw_text_centered(96, "NO SD", COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }
    if(!fat_mounted)
    {
        lcd_draw_text_centered(96, "FAT32 ONLY", COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }
    if(count == 0)
    {
        lcd_draw_text_centered(96, "EMPTY", COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }

    for(uint8_t row = 0; row < FILES_ROWS; row++)
        files_view_draw_row_impl(row, items, count, top, index);
}

static void files_view_render_preview_impl(const char *preview, uint16_t len, uint8_t page)
{
    uint16_t chars_per_page = FILE_PREVIEW_ROWS * TERM_COLS;
    uint16_t start = (uint16_t)page * chars_per_page;

    menu_draw_chrome("PREVIEW");
    if(len == 0)
    {
        lcd_draw_text_centered(96, "EMPTY", COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }

    for(uint8_t row = 0; row < FILE_PREVIEW_ROWS; row++)
    {
        char line[TERM_COLS + 1];
        uint16_t offset = start + row * TERM_COLS;
        memset(line, ' ', TERM_COLS);
        line[TERM_COLS] = '\0';
        for(uint8_t col = 0; col < TERM_COLS && offset + col < len; col++)
            line[col] = preview[offset + col];
        lcd_draw_text_at(0, FILES_ROW_Y0 + row * HACKYLENS_FONT_H, line, COLOR_TERM_GREEN, COLOR_BLACK);
    }
}

static void files_view_clear_image_impl(void)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
}

static void files_view_render_rgb888_scaled_row_at(uint16_t x0, uint16_t y, uint16_t dst_w, const uint8_t *row, uint16_t src_w, uint8_t bgr_order)
{
    for(uint16_t x = 0; x < dst_w; x++)
    {
        uint32_t src_x = (uint32_t)x * src_w / dst_w;
        const uint8_t *px = &row[src_x * 3U];
        uint8_t r = bgr_order ? px[2] : px[0];
        uint8_t g = px[1];
        uint8_t b = bgr_order ? px[0] : px[2];
        uint16_t color = (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                                    ((uint16_t)(g & 0xFCU) << 3) |
                                    ((uint16_t)b >> 3));
        lcd_line_buffer()[x * 2U + 0U] = color >> 8;
        lcd_line_buffer()[x * 2U + 1U] = color & 0xFF;
    }
    lcd_set_window(x0, y, x0 + dst_w - 1U, y);
    lcd_write_pixels(lcd_line_buffer(), dst_w * 2U);
}

static void files_view_render_rgb565le_scaled_row_at(uint16_t x0, uint16_t y, uint16_t dst_w, const uint8_t *row, uint16_t src_w)
{
    for(uint16_t x = 0; x < dst_w; x++)
    {
        uint32_t src_x = (uint32_t)x * src_w / dst_w;
        uint16_t color = (uint16_t)row[src_x * 2U] | ((uint16_t)row[src_x * 2U + 1U] << 8);
        lcd_line_buffer()[x * 2U + 0U] = color >> 8;
        lcd_line_buffer()[x * 2U + 1U] = color & 0xFF;
    }
    lcd_set_window(x0, y, x0 + dst_w - 1U, y);
    lcd_write_pixels(lcd_line_buffer(), dst_w * 2U);
}

static void files_view_render_image_row_span_impl(uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order)
{
    uint16_t dst_x;
    uint16_t dst_y;
    uint16_t dst_w;
    uint16_t dst_h;
    uint16_t y0;
    uint16_t y1;

    image_fit_viewport(src_w, (uint16_t)src_h, &dst_x, &dst_y, &dst_w, &dst_h);
    y0 = (uint16_t)(dst_y + src_y * dst_h / src_h);
    y1 = (uint16_t)(dst_y + ((src_y + 1U) * dst_h + src_h - 1U) / src_h);

    if(y1 <= y0)
        y1 = (uint16_t)(y0 + 1U);
    if(y1 > dst_y + dst_h)
        y1 = (uint16_t)(dst_y + dst_h);

    for(uint16_t y = y0; y < y1; y++)
    {
        if(bpp == 16)
            files_view_render_rgb565le_scaled_row_at(dst_x, y, dst_w, row, src_w);
        else
            files_view_render_rgb888_scaled_row_at(dst_x, y, dst_w, row, src_w, bgr_order);
    }
}

static void files_view_draw_delete_confirm_impl(const char *name)
{
    menu_draw_chrome("DELETE");
    lcd_draw_text_centered(68, "do u want delete?", COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_centered(104, name, COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_centered(152, "OK DELETE", COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_centered(182, "BACK CANCEL", COLOR_TERM_GREEN, COLOR_BLACK);
}

void files_view_init(void)
{
    static const files_view_ops_t ops = {
        .enter = files_view_enter_impl,
        .draw_status = files_view_draw_status_impl,
        .draw_row = files_view_draw_row_impl,
        .render_list = files_view_render_list_impl,
        .render_preview = files_view_render_preview_impl,
        .clear_image = files_view_clear_image_impl,
        .render_image_row_span = files_view_render_image_row_span_impl,
        .draw_delete_confirm = files_view_draw_delete_confirm_impl,
    };

    files_view_register(&ops);
}
