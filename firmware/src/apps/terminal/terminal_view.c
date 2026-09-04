#include "terminal_view.h"

#include "terminal_buffer.h"
#include "terminal_config.h"
#include "terminal_firmware.h"

#define TERMINAL_MAX_VIEW_COLUMNS \
    (HK_DISPLAY_REQUIRED_WIDTH / (HACKYLENS_FONT_W / TERMINAL_SMALL_SCALE))

terminal_geometry_t terminal_view_geometry(terminal_font_size_t font_size)
{
    uint16_t scale = font_size == TERMINAL_FONT_SMALL ?
        TERMINAL_SMALL_SCALE : TERMINAL_NORMAL_SCALE;
    terminal_geometry_t geometry = {
        .columns = (uint16_t)(HK_DISPLAY_REQUIRED_WIDTH /
                              (HACKYLENS_FONT_W / scale)),
        .rows = (uint16_t)(HK_DISPLAY_REQUIRED_HEIGHT /
                           (HACKYLENS_FONT_H / scale)),
        .cell_width = (uint16_t)(HACKYLENS_FONT_W / scale),
        .cell_height = (uint16_t)(HACKYLENS_FONT_H / scale),
    };

    return geometry;
}

static uint8_t terminal_glyph_pixel(char c, uint16_t x, uint16_t y, uint16_t scale)
{
    const uint8_t *glyph = hk_font_glyph((uint32_t)(uint8_t)c);

    for(uint16_t dy = 0U; dy < scale; dy++)
    {
        for(uint16_t dx = 0U; dx < scale; dx++)
        {
            uint16_t source_x = x * scale + dx;
            uint16_t source_y = y * scale + dy;
            uint8_t bit = glyph[source_y * HACKYLENS_FONT_ROW_BYTES +
                                source_x / 8U] &
                          (uint8_t)(0x80U >> (source_x & 7U));
            if(bit)
                return 1U;
        }
    }
    return 0U;
}

static uint8_t terminal_indicator_pixel(uint16_t x,
                                        uint16_t y,
                                        uint16_t content_width,
                                        uint16_t width,
                                        uint16_t height,
                                        const terminal_buffer_status_t *status,
                                        uint16_t *color)
{
    const uint16_t track_y = 2U;
    uint16_t track_h;
    uint16_t indicator_x;
    uint16_t thumb_h;
    uint16_t thumb_y;
    uint32_t top;

    if(height < 5U || content_width >= width || !status)
        return 0U;
    track_h = (uint16_t)(height - 4U);
    indicator_x = (uint16_t)(content_width + (width - content_width - 2U) / 2U);
    if(x < indicator_x || x >= indicator_x + 2U || y < track_y ||
       y >= track_y + track_h)
        return 0U;

    if(status->max_scroll_offset == 0U)
    {
        if(y >= track_y + track_h - 4U)
        {
            *color = COLOR_TERM_GREEN;
            return 1U;
        }
        return 0U;
    }

    thumb_h = (uint16_t)(((uint32_t)track_h * status->visible_rows) /
                         status->total_rows);
    if(thumb_h < 4U)
        thumb_h = 4U;
    if(thumb_h > track_h)
        thumb_h = track_h;
    top = status->max_scroll_offset - status->scroll_offset;
    thumb_y = (uint16_t)(track_y + (top * (track_h - thumb_h)) /
                         status->max_scroll_offset);
    if(y >= thumb_y && y < thumb_y + thumb_h)
    {
        *color = status->auto_follow ? COLOR_TERM_GREEN : COLOR_WHITE;
        return 1U;
    }
    if(x == indicator_x && (y == track_y || y == track_y + track_h - 1U))
    {
        *color = COLOR_TERM_GREEN;
        return 1U;
    }
    return 0U;
}

static void terminal_write_pixel(
    uint8_t *row, uint16_t x, uint16_t color)
{
    row[x * 2U] = (uint8_t)(color >> 8);
    row[x * 2U + 1U] = (uint8_t)(color & 0xFFU);
}

hk_result_t terminal_view_render(
    hk_app_surface_t *surface, terminal_font_size_t font_size)
{
    hk_display_surface_t pixels;
    terminal_geometry_t geometry = terminal_view_geometry(font_size);
    terminal_buffer_status_t status = terminal_buffer_status();
    uint16_t scale = font_size == TERMINAL_FONT_SMALL ?
        TERMINAL_SMALL_SCALE : TERMINAL_NORMAL_SCALE;
    uint16_t content_width = geometry.columns * geometry.cell_width;
    char row_text[TERMINAL_MAX_VIEW_COLUMNS + 1U];
    uint16_t loaded_row = UINT16_MAX;
    uint16_t width;
    uint16_t height;
    hk_result_t result;

    result = hk_app_surface_lock(surface, &pixels);
    if(result != HK_OK)
        return result;
    if(!pixels.pixels.data || pixels.pixels.stride_bytes == 0U ||
       pixels.width == 0U || pixels.height == 0U)
        return HK_ERR_INTERNAL;
    width = pixels.width > HK_DISPLAY_REQUIRED_WIDTH ?
        HK_DISPLAY_REQUIRED_WIDTH : (uint16_t)pixels.width;
    height = pixels.height > HK_DISPLAY_REQUIRED_HEIGHT ?
        HK_DISPLAY_REQUIRED_HEIGHT : (uint16_t)pixels.height;
    for(uint16_t y = 0U; y < height; y++)
    {
        uint8_t *row = (uint8_t *)pixels.pixels.data +
            (uint32_t)y * pixels.pixels.stride_bytes;
        uint16_t text_row = geometry.cell_height == 0U ?
            0U : (uint16_t)(y / geometry.cell_height);

        if(text_row != loaded_row)
        {
            terminal_buffer_visible_row(
                text_row, row_text, (size_t)geometry.columns + 1U);
            loaded_row = text_row;
        }
        for(uint16_t x = 0U; x < width; x++)
        {
            uint16_t color = COLOR_BLACK;
            uint16_t col = geometry.cell_width == 0U ?
                0U : (uint16_t)(x / geometry.cell_width);

            if(col < geometry.columns && text_row < geometry.rows &&
               terminal_glyph_pixel(row_text[col],
                                    x % geometry.cell_width,
                                    y % geometry.cell_height,
                                    scale))
                color = COLOR_TERM_GREEN;
            terminal_indicator_pixel(
                x, y, content_width, width, height, &status, &color);
            terminal_write_pixel(row, x, color);
        }
    }
    return hk_app_surface_invalidate(surface, NULL);
}
