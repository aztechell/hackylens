#include "hk_lcd.h"

#include <stdio.h>
#include <string.h>

#if defined(LCD_ST7789_TESTING)
#ifndef HK_ENABLE_APP_MICROPYTHON
#define HK_ENABLE_APP_MICROPYTHON 1
#endif
#else
#include "hk_config.h"
#endif

#include "defaults.h"
#include "../config/display_config.h"
#include "../core/hk_string.h"
#include "hackylens_boot_logo_1bpp.h"

#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_spi.h"
#include "hal_time.h"

static uint8_t g_line[LCD_W * 2];
#if HK_ENABLE_APP_MICROPYTHON
static uint8_t g_lcd_tx_line[LCD_W * 2U];
#define LCD_OVERLAY_REPAINT_TIMEOUT_US 500000ULL
#endif
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

static uint8_t lcd_set_window_physical(uint16_t x0, uint16_t y0,
                                       uint16_t x1, uint16_t y1);

void lcd_driver_prepare(void)
{
    hal_pwm_init(SCREEN_BL_PWM_DEVICE);
    hal_spi_init(LCD_SPI, 32);
    printf("[LCD] spi original-ish clk=%u\r\n",
           hal_spi_set_clock(LCD_SPI, LCD_SPI_HZ));
}

#if HK_ENABLE_APP_MICROPYTHON
static uint8_t lcd_set_window_physical_until(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
    uint64_t deadline_us);
static lcd_overlay_present_result_t lcd_overlay_repaint(
    const lcd_overlay_command_t *commands, size_t command_count,
    const uint8_t *text, size_t text_length,
    lcd_overlay_cancel_fn cancelled, void *cancel_context);
static lcd_overlay_command_t
    g_lcd_overlay_commands[LCD_OVERLAY_COMMAND_MAX];
static uint8_t g_lcd_overlay_text[LCD_OVERLAY_TEXT_MAX];
static size_t g_lcd_overlay_command_count;
static size_t g_lcd_overlay_text_length;
static uint32_t g_lcd_overlay_run_id;
static uint8_t g_lcd_overlay_visible;
/* True means the panel may contain only a prefix of a frame.  It is cleared
 * only after a complete physical repaint, never merely by logical cleanup. */
static uint8_t g_lcd_overlay_physical_dirty;
#endif

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

static uint8_t lcd_spi_send_bytes(const uint8_t *data, size_t len)
{
    lcd_spi_set_tmod_tx();
    return hal_spi_fifo_send_bytes(LCD_SPI, LCD_CS, data, len);
}

#if HK_ENABLE_APP_MICROPYTHON
static uint8_t lcd_spi_send_bytes_until(const uint8_t *data, size_t len,
                                        uint64_t deadline_us)
{
    lcd_spi_set_tmod_tx();
    return hal_spi_fifo_send_bytes_until(LCD_SPI, LCD_CS, data, len,
                                         deadline_us);
}
#endif

static uint8_t lcd_cmd(uint8_t cmd)
{
    lcd_spi_set_frame_bits(8);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 0);
    return lcd_spi_send_bytes(&cmd, 1);
}

static uint8_t lcd_data(const uint8_t *data, size_t len)
{
    lcd_spi_set_frame_bits(8);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 1);
    return lcd_spi_send_bytes(data, len);
}

#if HK_ENABLE_APP_MICROPYTHON
static uint8_t lcd_cmd_until(uint8_t cmd, uint64_t deadline_us)
{
    lcd_spi_set_frame_bits(8);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 0);
    return lcd_spi_send_bytes_until(&cmd, 1U, deadline_us);
}

static uint8_t lcd_data_until(const uint8_t *data, size_t len,
                              uint64_t deadline_us)
{
    lcd_spi_set_frame_bits(8);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 1);
    return lcd_spi_send_bytes_until(data, len, deadline_us);
}

static uint64_t lcd_overlay_deadline(void)
{
    uint64_t now = hal_time_us();

    return UINT64_MAX - now < LCD_OVERLAY_REPAINT_TIMEOUT_US
               ? UINT64_MAX
               : now + LCD_OVERLAY_REPAINT_TIMEOUT_US;
}
#endif

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

#if HK_ENABLE_APP_MICROPYTHON
static uint32_t lcd_overlay_utf8_next(const uint8_t *text, size_t end,
                                      size_t *position)
{
    uint8_t first;
    uint32_t codepoint;
    size_t required;

    if(!text || !position || *position >= end)
        return 0U;
    first = text[(*position)++];
    if(first < 0x80U)
        return first;
    if((first & 0xE0U) == 0xC0U)
    {
        codepoint = first & 0x1FU;
        required = 1U;
    }
    else if((first & 0xF0U) == 0xE0U)
    {
        codepoint = first & 0x0FU;
        required = 2U;
    }
    else if((first & 0xF8U) == 0xF0U)
    {
        codepoint = first & 0x07U;
        required = 3U;
    }
    else
    {
        return '?';
    }
    if(required > end - *position)
    {
        *position = end;
        return '?';
    }
    for(size_t i = 0U; i < required; i++)
    {
        uint8_t next = text[*position];
        if((next & 0xC0U) != 0x80U)
            return '?';
        (*position)++;
        codepoint = (codepoint << 6) | (next & 0x3FU);
    }
    return codepoint;
}

static void lcd_overlay_line_fill(uint8_t *line, uint32_t x0,
                                  uint32_t x1, uint16_t color)
{
    if(x0 >= LCD_W)
        return;
    if(x1 > LCD_W)
        x1 = LCD_W;
    for(uint32_t x = x0; x < x1; x++)
    {
        line[x * 2U] = (uint8_t)(color >> 8);
        line[x * 2U + 1U] = (uint8_t)color;
    }
}

static void lcd_overlay_render_text_row(
    uint8_t *line, uint16_t y, const lcd_overlay_command_t *command,
    const uint8_t *text, size_t text_length)
{
    uint32_t y0 = command->y;
    uint32_t glyph_row;
    uint32_t glyph_x = command->x;
    size_t position = command->text_offset;
    size_t end = position + command->text_length;

    if((uint32_t)y < y0 || (uint32_t)y >= y0 + HACKYLENS_FONT_H ||
       end > text_length)
        return;
    glyph_row = (uint32_t)y - y0;
    while(position < end && glyph_x < LCD_W)
    {
        const uint8_t *glyph = term_glyph(
            lcd_overlay_utf8_next(text, end, &position));

        for(uint32_t glyph_x_offset = 0U;
            glyph_x_offset < HACKYLENS_FONT_W; glyph_x_offset++)
        {
            uint32_t x = glyph_x + glyph_x_offset;
            uint8_t bit;
            uint16_t color;

            if(x >= LCD_W)
                break;
            bit = glyph[glyph_row * HACKYLENS_FONT_ROW_BYTES +
                        glyph_x_offset / 8U] &
                  (uint8_t)(0x80U >> (glyph_x_offset & 7U));
            color = bit ? command->color : command->background;
            line[x * 2U] = (uint8_t)(color >> 8);
            line[x * 2U + 1U] = (uint8_t)color;
        }
        glyph_x += HACKYLENS_FONT_W;
    }
}

static void lcd_overlay_render_row(
    uint8_t *line, uint16_t y, const lcd_overlay_command_t *commands,
    size_t command_count, const uint8_t *text, size_t text_length)
{
    memcpy(line, g_lcd_shadow + (uint32_t)y * LCD_W * 2U, LCD_W * 2U);
    for(size_t index = 0U; index < command_count; index++)
    {
        const lcd_overlay_command_t *command = &commands[index];
        uint32_t x0 = command->x;
        uint32_t y0 = command->y;
        uint32_t x1 = x0 + command->width;
        uint32_t y1 = y0 + command->height;

        switch(command->type)
        {
        case LCD_OVERLAY_COMMAND_CLEAR:
            lcd_overlay_line_fill(line, 0U, LCD_W, command->color);
            break;
        case LCD_OVERLAY_COMMAND_TEXT:
            lcd_overlay_render_text_row(line, y, command,
                                        text, text_length);
            break;
        case LCD_OVERLAY_COMMAND_RECT:
            if((uint32_t)y < y0 || (uint32_t)y >= y1)
                break;
            if(command->filled || (uint32_t)y == y0 ||
               (uint32_t)y + 1U == y1)
            {
                lcd_overlay_line_fill(line, x0, x1, command->color);
            }
            else
            {
                lcd_overlay_line_fill(line, x0, x0 + 1U,
                                      command->color);
                if(x1 != 0U)
                    lcd_overlay_line_fill(line, x1 - 1U, x1,
                                          command->color);
            }
            break;
        default:
            break;
        }
    }
}

static uint8_t lcd_overlay_commands_valid(
    const lcd_overlay_command_t *commands, size_t command_count,
    const uint8_t *text, size_t text_length)
{
    if(command_count > LCD_OVERLAY_COMMAND_MAX ||
       text_length > LCD_OVERLAY_TEXT_MAX ||
       (command_count && !commands) || (text_length && !text))
        return 0U;
    for(size_t index = 0U; index < command_count; index++)
    {
        const lcd_overlay_command_t *command = &commands[index];

        if(command->type == LCD_OVERLAY_COMMAND_CLEAR)
            continue;
        if(command->type == LCD_OVERLAY_COMMAND_RECT)
        {
            if(!command->width || !command->height)
                return 0U;
            continue;
        }
        if(command->type != LCD_OVERLAY_COMMAND_TEXT ||
           command->text_offset > text_length ||
           command->text_length > text_length - command->text_offset)
            return 0U;
    }
    return 1U;
}
#endif

void lcd_write_pixels(const uint8_t *data, size_t len)
{
#if HK_ENABLE_APP_MICROPYTHON
    if(g_lcd_overlay_visible && data && len >= 2U)
    {
        uint16_t x = g_lcd_cursor_x;
        uint16_t y = g_lcd_cursor_y;
        size_t pixels = len / 2U;
        uint8_t was_dirty = g_lcd_overlay_physical_dirty;

        /* The base is updated first.  The transmitted bytes are then rebuilt
         * from that latest base plus the immutable presented overlay. */
        lcd_shadow_write_pixels(data, len);
        g_lcd_overlay_physical_dirty = 1U;
        while(pixels && y <= g_lcd_window_y1)
        {
            size_t count = (size_t)g_lcd_window_x1 - x + 1U;
            if(count > pixels)
                count = pixels;
            lcd_overlay_render_row(
                g_lcd_tx_line, y, g_lcd_overlay_commands,
                g_lcd_overlay_command_count, g_lcd_overlay_text,
                g_lcd_overlay_text_length);
            if(!lcd_data(g_lcd_tx_line + (uint32_t)x * 2U, count * 2U))
                return;
            pixels -= count;
            x = g_lcd_window_x0;
            y++;
        }
        if(!was_dirty)
            g_lcd_overlay_physical_dirty = 0U;
        return;
    }
#endif
#if HK_ENABLE_APP_MICROPYTHON
    if(!lcd_data(data, len) && g_lcd_overlay_run_id)
        g_lcd_overlay_physical_dirty = 1U;
#else
    (void)lcd_data(data, len);
#endif
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
    uint8_t result;

    if(!g_lcd_frame_leased || lease_id == 0 || lease_id != g_lcd_frame_lease_id)
        return 0;

#if HK_ENABLE_APP_MICROPYTHON
    if(g_lcd_overlay_visible)
    {
        result = lcd_overlay_repaint(
                     g_lcd_overlay_commands,
                     g_lcd_overlay_command_count,
                     g_lcd_overlay_text, g_lcd_overlay_text_length,
                     NULL, NULL) == LCD_OVERLAY_PRESENT_OK;
    }
    else
    {
        uint64_t deadline_us = lcd_overlay_deadline();

        /* The common camera path stays one contiguous pixel transfer when no
         * script overlay is visible. */
        g_lcd_overlay_physical_dirty = 1U;
        result = lcd_set_window_physical_until(
                     0U, 0U, LCD_W - 1U, LCD_H - 1U, deadline_us) &&
                 lcd_data_until(g_lcd_shadow, sizeof(g_lcd_shadow),
                                deadline_us);
        if(result)
            g_lcd_overlay_physical_dirty = 0U;
    }
#else
    result = lcd_set_window_physical(0U, 0U, LCD_W - 1U, LCD_H - 1U) &&
             lcd_data(g_lcd_shadow, sizeof(g_lcd_shadow));
#endif
    g_lcd_frame_leased = 0;
    g_lcd_frame_lease_id = 0;
    return result;
}

void lcd_frame_cancel(uint32_t lease_id)
{
    if(!g_lcd_frame_leased || lease_id == 0 || lease_id != g_lcd_frame_lease_id)
        return;
    g_lcd_frame_leased = 0;
    g_lcd_frame_lease_id = 0;
}

#if HK_ENABLE_APP_MICROPYTHON
static lcd_overlay_present_result_t lcd_overlay_repaint(
    const lcd_overlay_command_t *commands, size_t command_count,
    const uint8_t *text, size_t text_length,
    lcd_overlay_cancel_fn cancelled, void *cancel_context)
{
    uint64_t deadline_us;

    if(cancelled && cancelled(cancel_context))
        return LCD_OVERLAY_PRESENT_CANCELLED;
    deadline_us = lcd_overlay_deadline();
    g_lcd_overlay_physical_dirty = 1U;
    if(!lcd_set_window_physical_until(
           0U, 0U, LCD_W - 1U, LCD_H - 1U, deadline_us))
        return LCD_OVERLAY_PRESENT_IO;

    if(command_count == 0U)
    {
        if(!lcd_data_until(g_lcd_shadow, sizeof(g_lcd_shadow), deadline_us))
            return LCD_OVERLAY_PRESENT_IO;
        if(cancelled && cancelled(cancel_context))
            return LCD_OVERLAY_PRESENT_CANCELLED;
        if(hal_time_us() > deadline_us)
            return LCD_OVERLAY_PRESENT_IO;
        g_lcd_overlay_physical_dirty = 0U;
        return LCD_OVERLAY_PRESENT_OK;
    }

    for(uint16_t y = 0U; y < LCD_H; y++)
    {
        if(cancelled && cancelled(cancel_context))
            return LCD_OVERLAY_PRESENT_CANCELLED;
        lcd_overlay_render_row(g_lcd_tx_line, y, commands, command_count,
                               text, text_length);
        if(!lcd_data_until(g_lcd_tx_line, LCD_W * 2U, deadline_us))
            return LCD_OVERLAY_PRESENT_IO;
    }
    if(cancelled && cancelled(cancel_context))
        return LCD_OVERLAY_PRESENT_CANCELLED;
    if(hal_time_us() > deadline_us)
        return LCD_OVERLAY_PRESENT_IO;
    g_lcd_overlay_physical_dirty = 0U;
    return LCD_OVERLAY_PRESENT_OK;
}

static uint8_t lcd_overlay_restore_presented(void)
{
    if(!g_lcd_overlay_physical_dirty)
        return 1U;
    return lcd_overlay_repaint(
               g_lcd_overlay_commands,
               g_lcd_overlay_visible ? g_lcd_overlay_command_count : 0U,
               g_lcd_overlay_text, g_lcd_overlay_text_length, NULL, NULL) ==
           LCD_OVERLAY_PRESENT_OK;
}

static void lcd_overlay_clear_logical(void)
{
    g_lcd_overlay_visible = 0U;
    g_lcd_overlay_command_count = 0U;
    g_lcd_overlay_text_length = 0U;
    g_lcd_overlay_run_id = 0U;
}

uint8_t lcd_overlay_acquire(uint32_t run_id)
{
    if(!run_id)
        return 0U;
    if(g_lcd_overlay_run_id)
        return g_lcd_overlay_run_id == run_id;

    /* A prior owner's failed cleanup must never wedge future runs.  Attempt
     * bounded base recovery, but grant logical ownership even if the panel is
     * still unavailable; a later present/release can repair it. */
    if(g_lcd_overlay_physical_dirty)
        (void)lcd_overlay_repaint(NULL, 0U, NULL, 0U, NULL, NULL);
    g_lcd_overlay_run_id = run_id;
    return 1U;
}

lcd_overlay_present_result_t lcd_overlay_present(
    uint32_t run_id, const lcd_overlay_command_t *commands,
    size_t command_count, const uint8_t *text, size_t text_length,
    lcd_overlay_cancel_fn cancelled, void *cancel_context)
{
    lcd_overlay_present_result_t result;

    if(!run_id || run_id != g_lcd_overlay_run_id ||
       !lcd_overlay_commands_valid(commands, command_count,
                                   text, text_length))
        return LCD_OVERLAY_PRESENT_INVALID;
    result = lcd_overlay_repaint(commands, command_count, text, text_length,
                                 cancelled, cancel_context);
    if(result != LCD_OVERLAY_PRESENT_OK)
    {
        /* A failed/cancelled transfer may have changed a prefix of the panel.
         * The prior logical overlay remains authoritative and is repainted on
         * a best-effort basis before returning. */
        (void)lcd_overlay_restore_presented();
        return result;
    }
    if(command_count)
        memcpy(g_lcd_overlay_commands, commands,
               command_count * sizeof(commands[0]));
    if(text_length)
        memcpy(g_lcd_overlay_text, text, text_length);
    g_lcd_overlay_command_count = command_count;
    g_lcd_overlay_text_length = text_length;
    g_lcd_overlay_visible = command_count != 0U;
    return LCD_OVERLAY_PRESENT_OK;
}

uint8_t lcd_overlay_release(uint32_t run_id)
{
    if(!g_lcd_overlay_run_id)
    {
        /* Idempotent cleanup is also the retry path after logical ownership
         * was dropped following an earlier physical failure. */
        if(g_lcd_overlay_physical_dirty)
            return lcd_overlay_repaint(NULL, 0U, NULL, 0U, NULL, NULL) ==
                   LCD_OVERLAY_PRESENT_OK;
        return 1U;
    }
    if(!run_id || run_id != g_lcd_overlay_run_id)
        return 0U;
    if((g_lcd_overlay_visible || g_lcd_overlay_physical_dirty) &&
       lcd_overlay_repaint(NULL, 0U, NULL, 0U, NULL, NULL) !=
           LCD_OVERLAY_PRESENT_OK)
    {
        /* The service cannot retain this failed ownership.  Preserve only
         * physical dirty so idempotent cleanup or a new run can retry. */
        lcd_overlay_clear_logical();
        return 0U;
    }
    lcd_overlay_clear_logical();
    return 1U;
}
#else
uint8_t lcd_overlay_acquire(uint32_t run_id)
{
    (void)run_id;
    return 0U;
}

lcd_overlay_present_result_t lcd_overlay_present(
    uint32_t run_id, const lcd_overlay_command_t *commands,
    size_t command_count, const uint8_t *text, size_t text_length,
    lcd_overlay_cancel_fn cancelled, void *cancel_context)
{
    (void)run_id;
    (void)commands;
    (void)command_count;
    (void)text;
    (void)text_length;
    (void)cancelled;
    (void)cancel_context;
    return LCD_OVERLAY_PRESENT_INVALID;
}

uint8_t lcd_overlay_release(uint32_t run_id)
{
    (void)run_id;
    return 0U;
}
#endif

static void lcd_data_u8(uint8_t value)
{
    lcd_data(&value, 1);
}

static void lcd_reset_like_original(void)
{
    hal_gpiohs_write(GPIOHS_LCD_RST, 1);
    hal_sleep_ms(1);
    hal_gpiohs_write(GPIOHS_LCD_RST, 0);
    hal_sleep_ms(1);
    hal_gpiohs_write(GPIOHS_LCD_RST, 1);
    hal_sleep_ms(125);
}

static uint8_t lcd_set_window_physical(uint16_t x0, uint16_t y0,
                                       uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    if(!lcd_cmd(0x2A))
        return 0U;
    data[0] = x0 >> 8;
    data[1] = x0 & 0xFF;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xFF;
    if(!lcd_data(data, sizeof(data)))
        return 0U;

    if(!lcd_cmd(0x2B))
        return 0U;
    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;
    if(!lcd_data(data, sizeof(data)))
        return 0U;

    return lcd_cmd(0x2C);
}

#if HK_ENABLE_APP_MICROPYTHON
static uint8_t lcd_set_window_physical_until(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
    uint64_t deadline_us)
{
    uint8_t data[4];

    if(!lcd_cmd_until(0x2AU, deadline_us))
        return 0U;
    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)x1;
    if(!lcd_data_until(data, sizeof(data), deadline_us) ||
       !lcd_cmd_until(0x2BU, deadline_us))
        return 0U;
    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)y1;
    return lcd_data_until(data, sizeof(data), deadline_us) &&
           lcd_cmd_until(0x2CU, deadline_us);
}
#endif

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t result;

    lcd_shadow_set_window(x0, y0, x1, y1);
    result = lcd_set_window_physical(
        g_lcd_window_x0, g_lcd_window_y0,
        g_lcd_window_x1, g_lcd_window_y1);
#if HK_ENABLE_APP_MICROPYTHON
    if(!result && g_lcd_overlay_run_id)
        g_lcd_overlay_physical_dirty = 1U;
#else
    (void)result;
#endif
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

const uint8_t *term_glyph(uint32_t codepoint)
{
    uint32_t glyph_index;

    if(codepoint == HACKYLENS_CYRILLIC_YO_UPPER)
        glyph_index = 0U;
    else if(codepoint >= HACKYLENS_CYRILLIC_FIRST && codepoint <= HACKYLENS_CYRILLIC_LAST)
        glyph_index = 1U + codepoint - HACKYLENS_CYRILLIC_FIRST;
    else if(codepoint == HACKYLENS_CYRILLIC_YO_LOWER)
        glyph_index = HACKYLENS_CYRILLIC_COUNT - 1U;
    else
    {
        if(codepoint < HACKYLENS_FONT_FIRST || codepoint > HACKYLENS_FONT_LAST)
            codepoint = '?';
        glyph_index = codepoint - HACKYLENS_FONT_FIRST;
        return &g_hackylens_font_1bpp[
            glyph_index * HACKYLENS_FONT_H * HACKYLENS_FONT_ROW_BYTES];
    }
    return &g_hackylens_font_cyrillic_1bpp[
        glyph_index * HACKYLENS_FONT_H * HACKYLENS_FONT_ROW_BYTES];
}

void lcd_draw_glyph_at(uint16_t x0, uint16_t y0, uint32_t codepoint, uint16_t fg, uint16_t bg)
{
    const uint8_t *glyph = term_glyph(codepoint);
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
        uint32_t codepoint = utf8_next(&text);
        lcd_draw_glyph_at(x, y, codepoint, fg, bg);
        x += HACKYLENS_FONT_W;
    }
}

void lcd_draw_text_centered(uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    uint16_t width = (uint16_t)(utf8_glyph_count(text) * HACKYLENS_FONT_W);
    uint16_t x = width < LCD_W ? (LCD_W - width) / 2 : 0;
    lcd_draw_text_at(x, y, text, fg, bg);
}
