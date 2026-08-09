#ifndef HK_LCD_H
#define HK_LCD_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *rgb565_be;
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
    uint32_t lease_id;
} lcd_frame_surface_t;

/* A MicroPython frame is staged as a small, bounded display list.  The LCD
 * driver keeps the last successfully presented list as a transparent overlay
 * over the authoritative firmware framebuffer.  This avoids a second 150 KiB
 * base framebuffer while still allowing camera/UI updates underneath it. */
#define LCD_OVERLAY_COMMAND_MAX 32U
#define LCD_OVERLAY_TEXT_MAX 1024U

typedef enum
{
    LCD_OVERLAY_COMMAND_CLEAR = 1,
    LCD_OVERLAY_COMMAND_TEXT,
    LCD_OVERLAY_COMMAND_RECT,
} lcd_overlay_command_type_t;

typedef struct
{
    uint8_t type;
    uint8_t filled;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t color;
    uint16_t background;
    uint16_t text_offset;
    uint16_t text_length;
} lcd_overlay_command_t;

typedef enum
{
    LCD_OVERLAY_PRESENT_OK = 0,
    LCD_OVERLAY_PRESENT_INVALID,
    LCD_OVERLAY_PRESENT_CANCELLED,
    LCD_OVERLAY_PRESENT_IO,
} lcd_overlay_present_result_t;

typedef uint8_t (*lcd_overlay_cancel_fn)(void *context);

void lcd_init_original_sequence(void);
void lcd_draw_boot_logo(void);
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_write_pixels(const uint8_t *data, size_t len);
uint8_t *lcd_line_buffer(void);
uint16_t lcd_shadow_pixel(uint16_t x, uint16_t y);
uint8_t lcd_frame_acquire(lcd_frame_surface_t *surface);
uint8_t lcd_frame_present(uint32_t lease_id);
void lcd_frame_cancel(uint32_t lease_id);
uint8_t lcd_overlay_acquire(uint32_t run_id);
lcd_overlay_present_result_t lcd_overlay_present(
    uint32_t run_id, const lcd_overlay_command_t *commands,
    size_t command_count, const uint8_t *text, size_t text_length,
    lcd_overlay_cancel_fn cancelled, void *cancel_context);
uint8_t lcd_overlay_release(uint32_t run_id);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t thickness, uint16_t color);
const uint8_t *term_glyph(uint32_t codepoint);
void lcd_draw_glyph_at(uint16_t x0, uint16_t y0, uint32_t codepoint, uint16_t fg, uint16_t bg);
void lcd_draw_text_at(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg);
void lcd_draw_text_centered(uint16_t y, const char *text, uint16_t fg, uint16_t bg);

#endif
