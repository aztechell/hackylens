#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display_config.h"
#include "hk_lcd.h"

static uint8_t g_rows[LCD_H][LCD_W * 2U];
static size_t g_row_count;
static size_t g_send_count;
static size_t g_frame_count;
static size_t g_row_transfer_count;
static size_t g_full_transfer_count;
static size_t g_pixel_transfer_count;
static size_t g_fail_on_send;
static size_t g_fail_from_send;
static uint8_t g_dc_high;
static uint64_t g_time_us;
static uint64_t g_send_duration_us;
static uint64_t g_deadlines[1024];
static size_t g_deadline_count;
static uint32_t g_cancel_checks;
static uint32_t g_cancel_on_check;

uint8_t hal_spi_fifo_send_bytes_until(uint8_t device, uint8_t chip_select,
                                      const uint8_t *data, size_t len,
                                      uint64_t deadline_us);

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static uint16_t captured_pixel(uint16_t x, uint16_t y)
{
    return (uint16_t)((uint16_t)g_rows[y][x * 2U] << 8) |
           g_rows[y][x * 2U + 1U];
}

static void capture_reset(void)
{
    memset(g_rows, 0, sizeof(g_rows));
    g_row_count = 0U;
    g_send_count = 0U;
    g_frame_count = 0U;
    g_row_transfer_count = 0U;
    g_full_transfer_count = 0U;
    g_pixel_transfer_count = 0U;
    g_fail_on_send = 0U;
    g_fail_from_send = 0U;
    g_dc_high = 1U;
    g_time_us = 0U;
    g_send_duration_us = 0U;
    g_deadline_count = 0U;
}

static uint8_t cancel_callback(void *context)
{
    (void)context;
    g_cancel_checks++;
    return g_cancel_on_check != 0U &&
           g_cancel_checks >= g_cancel_on_check;
}

static void seed_shadow(uint16_t color)
{
    lcd_frame_surface_t surface;

    require_true(lcd_frame_acquire(&surface),
                 "shadow fixture must acquire a frame lease");
    require_true(surface.width == LCD_W && surface.height == LCD_H &&
                 surface.stride_bytes == LCD_W * 2U,
                 "frame surface geometry must match the panel");
    for(size_t pixel = 0U; pixel < (size_t)LCD_W * LCD_H; pixel++)
    {
        surface.rgb565_be[pixel * 2U] = (uint8_t)(color >> 8);
        surface.rgb565_be[pixel * 2U + 1U] = (uint8_t)color;
    }
    lcd_frame_cancel(surface.lease_id);
}

static void test_overlay_composes_without_mutating_base(void)
{
    const uint32_t run_id = 7U;
    lcd_overlay_command_t command = {
        .type = LCD_OVERLAY_COMMAND_RECT,
        .filled = 1U,
        .x = 2U,
        .y = 3U,
        .width = 2U,
        .height = 2U,
        .color = 0xF800U,
    };

    seed_shadow(0x1234U);
    require_true(lcd_overlay_acquire(run_id),
                 "overlay lease must be acquired");
    require_true(lcd_overlay_acquire(run_id),
                 "same-run overlay acquire must be idempotent");
    require_true(!lcd_overlay_acquire(run_id + 1U),
                 "another run must not steal the overlay lease");
    capture_reset();
    require_true(lcd_overlay_present(run_id, &command, 1U, NULL, 0U,
                                     NULL, NULL) ==
                     LCD_OVERLAY_PRESENT_OK,
                 "bounded overlay frame must present");
    require_true(g_row_count == LCD_H,
                 "present must repaint exactly one complete frame");
    require_true(captured_pixel(2U, 2U) == 0x1234U &&
                 captured_pixel(2U, 3U) == 0xF800U &&
                 captured_pixel(3U, 4U) == 0xF800U &&
                 captured_pixel(2U, 5U) == 0x1234U,
                 "overlay rectangle must be clipped to its requested rows");
    require_true(lcd_shadow_pixel(2U, 3U) == 0x1234U,
                 "overlay present must not mutate firmware base pixels");
}

static void test_base_updates_remain_behind_visible_overlay(void)
{
    static const uint8_t base_pixel[2] = {0x07U, 0xE0U};
    lcd_frame_surface_t surface;

    lcd_set_window(2U, 3U, 2U, 3U);
    capture_reset();
    lcd_write_pixels(base_pixel, sizeof(base_pixel));
    require_true(lcd_shadow_pixel(2U, 3U) == 0x07E0U,
                 "base write must update the firmware shadow");
    require_true(g_send_count != 0U,
                 "base write must still transmit while overlay is visible");

    require_true(lcd_frame_acquire(&surface),
                 "camera-style frame lease must remain available");
    surface.rgb565_be[((size_t)3U * LCD_W + 2U) * 2U] = 0x00U;
    surface.rgb565_be[((size_t)3U * LCD_W + 2U) * 2U + 1U] = 0x1FU;
    capture_reset();
    require_true(lcd_frame_present(surface.lease_id),
                 "camera-style base frame must present");
    require_true(g_row_count == LCD_H &&
                 captured_pixel(2U, 3U) == 0xF800U &&
                 lcd_shadow_pixel(2U, 3U) == 0x001FU,
                 "visible overlay must compose over the latest base frame");
}

static void test_cancel_and_io_failure_restore_prior_overlay(void)
{
    const uint32_t run_id = 7U;
    lcd_overlay_command_t replacement = {
        .type = LCD_OVERLAY_COMMAND_CLEAR,
        .color = 0xFFFFU,
    };

    g_cancel_checks = 0U;
    g_cancel_on_check = 12U;
    capture_reset();
    g_cancel_on_check = 12U;
    require_true(lcd_overlay_present(
                     run_id, &replacement, 1U, NULL, 0U,
                     cancel_callback, NULL) == LCD_OVERLAY_PRESENT_CANCELLED,
                 "cancelled overlay transfer must be reported");
    require_true(g_frame_count == 2U && g_row_count == LCD_H &&
                 captured_pixel(2U, 3U) == 0xF800U &&
                 captured_pixel(1U, 1U) == 0x1234U,
                 "mid-frame cancel must restore the prior overlay over base");

    g_cancel_on_check = 0U;
    capture_reset();
    g_fail_on_send = 18U;
    require_true(lcd_overlay_present(
                     run_id, &replacement, 1U, NULL, 0U,
                     NULL, NULL) == LCD_OVERLAY_PRESENT_IO,
                 "SPI failure must be surfaced by display present");
    require_true(g_fail_on_send == 0U && g_frame_count == 2U &&
                 g_row_count == LCD_H &&
                 captured_pixel(2U, 3U) == 0xF800U,
                 "mid-frame one-shot failure must restore the prior overlay");
}

static void test_global_deadline_is_shared_by_every_row(void)
{
    const uint32_t run_id = 7U;
    lcd_overlay_command_t replacement = {
        .type = LCD_OVERLAY_COMMAND_CLEAR,
        .color = 0xFFFFU,
    };
    uint64_t first_deadline;
    size_t deadline_change = 0U;

    capture_reset();
    g_send_duration_us = 4000U;
    require_true(lcd_overlay_present(
                     run_id, &replacement, 1U, NULL, 0U,
                     NULL, NULL) == LCD_OVERLAY_PRESENT_IO,
                 "a frame exceeding the global budget must time out");
    require_true(g_time_us <= 1000000U,
                 "failed repaint plus restore must stay within one second");
    require_true(g_deadline_count > 2U,
                 "deadline fixture must observe bounded SPI segments");
    first_deadline = g_deadlines[0];
    for(size_t index = 1U; index < g_deadline_count; index++)
    {
        if(g_deadlines[index] != first_deadline)
        {
            deadline_change = index;
            break;
        }
    }
    require_true(first_deadline == 500000U && deadline_change != 0U,
                 "all first-frame rows must share one 500 ms deadline");
    for(size_t index = 0U; index < deadline_change; index++)
        require_true(g_deadlines[index] == first_deadline,
                     "the repaint deadline must never reset per row");

    /* Repair the deliberately timed-out restore before the next case. */
    capture_reset();
    replacement.type = LCD_OVERLAY_COMMAND_RECT;
    replacement.filled = 1U;
    replacement.x = 2U;
    replacement.y = 3U;
    replacement.width = 2U;
    replacement.height = 2U;
    replacement.color = 0xF800U;
    require_true(lcd_overlay_present(run_id, &replacement, 1U,
                                     NULL, 0U, NULL, NULL) ==
                     LCD_OVERLAY_PRESENT_OK,
                 "a later successful present must clear physical dirty");
}

static void test_persistent_failure_never_wedges_future_runs(void)
{
    const uint32_t run_id = 7U;
    const uint32_t next_run_id = 8U;
    lcd_overlay_command_t replacement = {
        .type = LCD_OVERLAY_COMMAND_CLEAR,
        .color = 0xFFFFU,
    };
    size_t sends_after_release;

    capture_reset();
    g_fail_from_send = 18U;
    require_true(lcd_overlay_present(run_id, &replacement, 1U, NULL, 0U,
                                     NULL, NULL) ==
                     LCD_OVERLAY_PRESENT_IO,
                 "persistent mid-frame failure must surface as I/O");
    require_true(!lcd_overlay_release(run_id),
                 "release must report a persistent restore failure");
    require_true(lcd_overlay_acquire(next_run_id),
                 "failed cleanup must not block a subsequent run");
    require_true(!lcd_overlay_acquire(next_run_id + 1U),
                 "the newly granted run must retain normal exclusivity");

    capture_reset();
    require_true(lcd_overlay_present(next_run_id, &replacement, 1U,
                                     NULL, 0U, NULL, NULL) ==
                     LCD_OVERLAY_PRESENT_OK,
                 "later present by the new run must recover physical dirty");
    g_fail_from_send = g_send_count + 1U;
    require_true(!lcd_overlay_release(next_run_id),
                 "new owner's failed cleanup must again drop logical ownership");

    /* No logical owner remains, but dirty makes this an actual bounded retry. */
    capture_reset();
    require_true(lcd_overlay_release(next_run_id),
                 "idempotent release must retry dirty base cleanup");
    require_true(g_full_transfer_count == 1U && g_row_count == LCD_H &&
                 captured_pixel(2U, 3U) == 0x001FU,
                 "release must repaint the latest firmware base");
    sends_after_release = g_send_count;
    require_true(lcd_overlay_release(next_run_id),
                 "second release must be harmless");
    require_true(g_send_count == sends_after_release,
                 "idempotent release must not repaint again");
}

static void test_frame_without_overlay_uses_one_pixel_transfer(void)
{
    lcd_frame_surface_t surface;
    uint64_t frame_deadline;

    require_true(lcd_frame_acquire(&surface),
                 "fast-path fixture must acquire a frame");
    capture_reset();
    g_send_duration_us = 1000U;
    require_true(lcd_frame_present(surface.lease_id),
                 "base-only frame must present");
    require_true(g_pixel_transfer_count == 1U &&
                 g_full_transfer_count == 1U &&
                 g_row_transfer_count == 0U,
                 "base-only frame must use one contiguous pixel transfer");
    frame_deadline = g_deadlines[0];
    require_true(frame_deadline == 500000U && g_deadline_count == 6U,
                 "base frame must have one window-plus-payload deadline");
    for(size_t index = 1U; index < g_deadline_count; index++)
        require_true(g_deadlines[index] == frame_deadline,
                     "base-frame setup and payload must share the deadline");

    require_true(lcd_frame_acquire(&surface),
                 "timeout fixture must acquire another frame");
    capture_reset();
    g_send_duration_us = 100000U;
    require_true(!lcd_frame_present(surface.lease_id),
                 "base frame beyond 500 ms must time out");
    require_true(g_time_us == 500000U && g_deadline_count == 6U,
                 "base-frame timeout must be globally bounded at 500 ms");
    for(size_t index = 1U; index < g_deadline_count; index++)
        require_true(g_deadlines[index] == g_deadlines[0],
                     "timed-out base frame must not reset per transfer");

    require_true(lcd_frame_acquire(&surface),
                 "dirty base frame must remain recoverable");
    capture_reset();
    require_true(lcd_frame_present(surface.lease_id),
                 "later base frame must repair physical dirty");
}

static void test_invalid_lists_never_reach_panel(void)
{
    lcd_overlay_command_t invalid = {
        .type = LCD_OVERLAY_COMMAND_RECT,
        .width = 0U,
        .height = 1U,
    };

    require_true(lcd_overlay_acquire(9U),
                 "validation fixture must acquire a fresh run");
    capture_reset();
    require_true(lcd_overlay_present(9U, &invalid, 1U, NULL, 0U,
                                     NULL, NULL) ==
                     LCD_OVERLAY_PRESENT_INVALID,
                 "zero-width rectangle must be rejected");
    require_true(g_send_count == 0U,
                 "invalid overlay must not touch the panel");
    require_true(lcd_overlay_release(9U),
                 "validation fixture must release cleanly");
}

int main(void)
{
    test_overlay_composes_without_mutating_base();
    test_base_updates_remain_behind_visible_overlay();
    test_cancel_and_io_failure_restore_prior_overlay();
    test_global_deadline_is_shared_by_every_row();
    test_persistent_failure_never_wedges_future_runs();
    test_frame_without_overlay_uses_one_pixel_transfer();
    test_invalid_lists_never_reach_panel();
    puts("LCD_OVERLAY_OK cases=8");
    return 0;
}

void hal_gpiohs_write(uint8_t pin, uint8_t high)
{
    (void)pin;
    g_dc_high = high;
}

void hal_spi_fifo_set_tmod_tx(uint8_t device) { (void)device; }
void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits)
{
    (void)device;
    (void)bits;
}

uint8_t hal_spi_fifo_send_bytes(uint8_t device, uint8_t chip_select,
                                const uint8_t *data, size_t len)
{
    return hal_spi_fifo_send_bytes_until(device, chip_select, data, len,
                                         g_time_us + 500000U);
}

uint8_t hal_spi_fifo_send_bytes_until(uint8_t device, uint8_t chip_select,
                                      const uint8_t *data, size_t len,
                                      uint64_t deadline_us)
{
    (void)device;
    (void)chip_select;
    require_true(data != NULL && len != 0U,
                 "LCD SPI sends must contain bytes");
    g_send_count++;
    require_true(g_deadline_count <
                     sizeof(g_deadlines) / sizeof(g_deadlines[0]),
                 "deadline capture must not overflow");
    g_deadlines[g_deadline_count++] = deadline_us;
    if(g_time_us >= deadline_us ||
       g_send_duration_us > deadline_us - g_time_us)
    {
        g_time_us = deadline_us;
        return 0U;
    }
    g_time_us += g_send_duration_us;
    if((g_fail_on_send && g_send_count == g_fail_on_send) ||
       (g_fail_from_send && g_send_count >= g_fail_from_send))
    {
        if(g_fail_on_send == g_send_count)
            g_fail_on_send = 0U;
        return 0U;
    }
    if(!g_dc_high && len == 1U && data[0] == 0x2CU)
    {
        g_row_count = 0U;
        g_frame_count++;
    }
    if(len == LCD_W * 2U)
    {
        require_true(g_row_count < LCD_H,
                     "one repaint must not exceed panel height");
        memcpy(g_rows[g_row_count++], data, len);
        g_row_transfer_count++;
        g_pixel_transfer_count++;
    }
    else if(len == sizeof(g_rows))
    {
        memcpy(g_rows, data, len);
        g_row_count = LCD_H;
        g_full_transfer_count++;
        g_pixel_transfer_count++;
    }
    return 1U;
}

uint64_t hal_time_us(void) { return g_time_us; }
void hal_sleep_ms(uint32_t duration_ms) { (void)duration_ms; }
