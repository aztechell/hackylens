#include "hk_lcd.h"

#include <string.h>

#include "../board/board_pins.h"
#include "../config/display_config.h"
#include "hackylens_boot_logo_1bpp.h"

#include "../hal/hal_gpio.h"
#include "../hal/hal_spi.h"
#include "../hal/hal_time.h"

static uint8_t g_line[LCD_W * 2];
static uint8_t g_lcd_shadow[LCD_W * LCD_H * 2U] __attribute__((aligned(4), section(".bss")));
static uint16_t g_lcd_window_x0;
static uint16_t g_lcd_window_y0;
static uint16_t g_lcd_window_x1;
static uint16_t g_lcd_window_y1;
static uint16_t g_lcd_cursor_x;
static uint16_t g_lcd_cursor_y;
static uint8_t g_glyph_pixels[HACKYLENS_FONT_W * HACKYLENS_FONT_H * 2];
static uint8_t g_lcd_frame_leased;
static uint32_t g_lcd_frame_lease_id;
static uint32_t g_lcd_next_lease_id;

uint8_t *lcd_line_buffer(void)
{
    return g_line;
}

uint16_t lcd_shadow_pixel(uint16_t x, uint16_t y)
{
    uint32_t offset;

    if(x >= LCD_W || y >= LCD_H)
        return COLOR_BLACK;
    offset = ((uint32_t)y * LCD_W + x) * 2U;
    return (uint16_t)((uint16_t)g_lcd_shadow[offset] << 8) | g_lcd_shadow[offset + 1U];
}

static void lcd_spi_set_tmod_tx(void)
{
    hal_spi_fifo_set_tmod_tx(LCD_SPI);
}

static void lcd_spi_set_frame_bits(uint32_t bits)
{
    hal_spi_fifo_set_frame_bits(LCD_SPI, bits);
}

static void lcd_spi_send_bytes(const uint8_t *data, size_t len)
{
    lcd_spi_set_tmod_tx();
    hal_spi_fifo_send_bytes(LCD_SPI, LCD_CS, data, len);
}

void lcd_cmd(uint8_t cmd)
{
    lcd_spi_set_frame_bits(8);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 0);
    lcd_spi_send_bytes(&cmd, 1);
}

void lcd_data(const uint8_t *data, size_t len)
{
    lcd_spi_set_frame_bits(8);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 1);
    lcd_spi_send_bytes(data, len);
}

void lcd_shadow_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if(x0 >= LCD_W)
        x0 = LCD_W - 1U;
    if(y0 >= LCD_H)
        y0 = LCD_H - 1U;
    if(x1 >= LCD_W)
        x1 = LCD_W - 1U;
    if(y1 >= LCD_H)
        y1 = LCD_H - 1U;
    if(x1 < x0)
        x1 = x0;
    if(y1 < y0)
        y1 = y0;

    g_lcd_window_x0 = x0;
    g_lcd_window_y0 = y0;
    g_lcd_window_x1 = x1;
    g_lcd_window_y1 = y1;
    g_lcd_cursor_x = x0;
    g_lcd_cursor_y = y0;
}

void lcd_shadow_write_pixels(const uint8_t *data, size_t len)
{
    size_t pixels = len / 2U;

    for(size_t i = 0; i < pixels; i++)
    {
        if(g_lcd_cursor_x < LCD_W && g_lcd_cursor_y < LCD_H)
        {
            uint32_t offset = ((uint32_t)g_lcd_cursor_y * LCD_W + g_lcd_cursor_x) * 2U;
            g_lcd_shadow[offset] = data[i * 2U];
            g_lcd_shadow[offset + 1U] = data[i * 2U + 1U];
        }

        if(g_lcd_cursor_x >= g_lcd_window_x1)
        {
            g_lcd_cursor_x = g_lcd_window_x0;
            if(g_lcd_cursor_y >= g_lcd_window_y1)
                break;
            g_lcd_cursor_y++;
        }
        else
        {
            g_lcd_cursor_x++;
        }
    }
}

void lcd_write_pixels(const uint8_t *data, size_t len)
{
    lcd_data(data, len);
    lcd_shadow_write_pixels(data, len);
}

uint8_t lcd_frame_acquire(lcd_frame_surface_t *surface)
{
    uint32_t lease_id;

    if(!surface || g_lcd_frame_leased)
        return 0;
    lease_id = ++g_lcd_next_lease_id;
    if(lease_id == 0)
        lease_id = ++g_lcd_next_lease_id;
    g_lcd_frame_leased = 1;
    g_lcd_frame_lease_id = lease_id;
    surface->rgb565_be = g_lcd_shadow;
    surface->width = LCD_W;
    surface->height = LCD_H;
    surface->stride_bytes = LCD_W * 2U;
    surface->lease_id = lease_id;
    return 1;
}

uint8_t lcd_frame_present(uint32_t lease_id)
{
    if(!g_lcd_frame_leased || lease_id == 0 || lease_id != g_lcd_frame_lease_id)
        return 0;

    lcd_set_window(0, 0, LCD_W - 1U, LCD_H - 1U);
    lcd_data(g_lcd_shadow, sizeof(g_lcd_shadow));
    g_lcd_frame_leased = 0;
    g_lcd_frame_lease_id = 0;
    return 1;
}

void lcd_frame_cancel(uint32_t lease_id)
{
    if(!g_lcd_frame_leased || lease_id == 0 || lease_id != g_lcd_frame_lease_id)
        return;
    g_lcd_frame_leased = 0;
    g_lcd_frame_lease_id = 0;
}

void lcd_data_u8(uint8_t value)
{
    lcd_data(&value, 1);
}

void lcd_reset_like_original(void)
{
    hal_gpiohs_write(GPIOHS_LCD_RST, 1);
    hal_sleep_ms(1);
    hal_gpiohs_write(GPIOHS_LCD_RST, 0);
    hal_sleep_ms(1);
    hal_gpiohs_write(GPIOHS_LCD_RST, 1);
    hal_sleep_ms(125);
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    lcd_cmd(0x2A);
    data[0] = x0 >> 8;
    data[1] = x0 & 0xFF;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xFF;
    lcd_data(data, sizeof(data));

    lcd_cmd(0x2B);
    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;
    lcd_data(data, sizeof(data));

    lcd_cmd(0x2C);
    lcd_shadow_set_window(x0, y0, x1, y1);
}

void lcd_init_original_sequence(void)
{
    static const uint8_t gamma_p[] = {
        0xD0, 0x05, 0x0A, 0x09, 0x08, 0x05, 0x2E,
        0x44, 0x45, 0x0F, 0x17, 0x16, 0x2B, 0x33,
    };
    static const uint8_t gamma_n[] = {
        0xD0, 0x05, 0x0A, 0x09, 0x08, 0x05, 0x2E,
        0x43, 0x45, 0x0F, 0x16, 0x16, 0x2B, 0x33,
    };
    static const uint8_t b2[] = {0x01, 0x01, 0x00, 0x01, 0x01};
    static const uint8_t d0[] = {0xA4, 0xA1};

    lcd_reset_like_original();

    lcd_cmd(0x11);

    lcd_cmd(0x36);
    lcd_data_u8(0xA0);

    lcd_cmd(0x3A);
    lcd_data_u8(0x05);

    lcd_cmd(0x21);

    lcd_cmd(0xB2);
    lcd_data(b2, sizeof(b2));

    lcd_cmd(0xB7);
    lcd_data_u8(0x75);

    lcd_cmd(0xBB);
    lcd_data_u8(0x22);

    lcd_cmd(0xC0);
    lcd_data_u8(0x2C);

    lcd_cmd(0xC2);
    lcd_data_u8(0x01);

    lcd_cmd(0xC3);
    lcd_data_u8(0x13);

    lcd_cmd(0xC4);
    lcd_data_u8(0x20);

    lcd_cmd(0xC6);
    lcd_data_u8(0xE1);

    lcd_cmd(0xD0);
    lcd_data(d0, sizeof(d0));

    lcd_cmd(0xD6);
    lcd_data_u8(0xA1);

    lcd_cmd(0xE0);
    lcd_data(gamma_p, sizeof(gamma_p));

    lcd_cmd(0xE1);
    lcd_data(gamma_n, sizeof(gamma_n));

    lcd_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    lcd_cmd(0x29);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if(x >= LCD_W || y >= LCD_H)
        return;
    if(x + w > LCD_W)
        w = LCD_W - x;
    if(y + h > LCD_H)
        h = LCD_H - y;

    for(uint16_t i = 0; i < w; i++)
    {
        g_line[i * 2 + 0] = color >> 8;
        g_line[i * 2 + 1] = color & 0xFF;
    }

    lcd_set_window(x, y, x + w - 1, y + h - 1);
    for(uint16_t row = 0; row < h; row++)
        lcd_write_pixels(g_line, w * 2);
}

void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t thickness, uint16_t color)
{
    if(w == 0 || h == 0 || thickness == 0)
        return;

    for(uint16_t i = 0; i < thickness; i++)
    {
        if(w <= i * 2 || h <= i * 2)
            break;
        lcd_fill_rect(x + i, y + i, w - i * 2, 1, color);
        lcd_fill_rect(x + i, y + h - 1 - i, w - i * 2, 1, color);
        lcd_fill_rect(x + i, y + i, 1, h - i * 2, color);
        lcd_fill_rect(x + w - 1 - i, y + i, 1, h - i * 2, color);
    }
}

void lcd_draw_boot_logo(void)
{
    lcd_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    for(uint16_t y = 0; y < LCD_H; y++)
    {
        for(uint16_t x = 0; x < LCD_W; x++)
        {
            uint8_t bit = g_hackylens_boot_logo_1bpp[y * HACKYLENS_BOOT_LOGO_ROW_BYTES + x / 8] & (0x80 >> (x & 7));
            uint16_t color = bit ? COLOR_TERM_GREEN : COLOR_BLACK;
            g_line[x * 2 + 0] = color >> 8;
            g_line[x * 2 + 1] = color & 0xFF;
        }
        lcd_write_pixels(g_line, LCD_W * 2);
    }
}

const uint8_t *term_glyph(char c)
{
    if(c < HACKYLENS_FONT_FIRST || c > HACKYLENS_FONT_LAST)
        c = '?';
    uint32_t glyph_index = (uint32_t)(c - HACKYLENS_FONT_FIRST);
    return &g_hackylens_font_1bpp[glyph_index * HACKYLENS_FONT_H * HACKYLENS_FONT_ROW_BYTES];
}

void lcd_draw_glyph_at(uint16_t x0, uint16_t y0, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *glyph = term_glyph(c);
    for(uint16_t y = 0; y < HACKYLENS_FONT_H; y++)
    {
        for(uint16_t x = 0; x < HACKYLENS_FONT_W; x++)
        {
            uint8_t bit = glyph[y * HACKYLENS_FONT_ROW_BYTES + x / 8] & (0x80 >> (x & 7));
            uint16_t color = bit ? fg : bg;
            uint32_t pos = (uint32_t)(y * HACKYLENS_FONT_W + x) * 2;
            g_glyph_pixels[pos + 0] = color >> 8;
            g_glyph_pixels[pos + 1] = color & 0xFF;
        }
    }

    if(x0 + HACKYLENS_FONT_W > LCD_W || y0 + HACKYLENS_FONT_H > LCD_H)
        return;
    lcd_set_window(x0, y0, x0 + HACKYLENS_FONT_W - 1, y0 + HACKYLENS_FONT_H - 1);
    lcd_write_pixels(g_glyph_pixels, sizeof(g_glyph_pixels));
}

void lcd_draw_text_at(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    while(*text && x + HACKYLENS_FONT_W <= LCD_W)
    {
        lcd_draw_glyph_at(x, y, *text++, fg, bg);
        x += HACKYLENS_FONT_W;
    }
}

void lcd_draw_text_centered(uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    uint16_t width = (uint16_t)(strlen(text) * HACKYLENS_FONT_W);
    uint16_t x = width < LCD_W ? (LCD_W - width) / 2 : 0;
    lcd_draw_text_at(x, y, text, fg, bg);
}
