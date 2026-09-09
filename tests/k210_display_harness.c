#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/display.h>

#include "display_normative_suite.h"
#include "lcd_st7789_transport.h"

#define TEST_WIDTH 320U
#define TEST_HEIGHT 240U
#define TEST_FRAME_BYTES (TEST_WIDTH * TEST_HEIGHT * 2U)

void hk_k210_display_test_reset(void);

static uint8_t s_shadow[TEST_FRAME_BYTES] __attribute__((aligned(4)));
static uint8_t s_panel[TEST_FRAME_BYTES] __attribute__((aligned(4)));
static uint64_t s_now_us;
static uint64_t s_transfer_bytes;
static uint32_t s_write_calls;
static uint32_t s_begin_calls;
static uint32_t s_end_calls;
static uint32_t s_abort_calls;
static uint8_t s_stream_active;
static hk_display_rect_t s_window;
static uint32_t s_window_pixels;
static hk_result_t s_fail_result;
static uint32_t s_fail_at_write;

typedef struct
{
    uint32_t probes;
    uint32_t cancel_after;
} cancel_fixture_t;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void lcd_st7789_transport_prepare(void) {}
void lcd_st7789_transport_init(void) {}

hk_result_t lcd_st7789_transport_begin(
    const hk_display_rect_t *rect, hk_deadline_t deadline)
{
    if(deadline.at_us && s_now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(s_stream_active)
        return HK_ERR_INVALID_STATE;
    if(!rect || rect->x < 0 || rect->y < 0 ||
       (uint64_t)(uint32_t)rect->x + rect->width > TEST_WIDTH ||
       (uint64_t)(uint32_t)rect->y + rect->height > TEST_HEIGHT)
        return HK_ERR_INVALID_ARGUMENT;
    s_window = *rect;
    s_window_pixels = 0U;
    s_begin_calls++;
    s_stream_active = 1U;
    return HK_OK;
}

hk_result_t lcd_st7789_transport_write(
    const uint8_t *pixels, size_t size_bytes,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(!s_stream_active || !pixels || size_bytes == 0U || size_bytes > 128U ||
       (size_bytes & 1U) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us && s_now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(s_fail_result != HK_OK && s_write_calls >= s_fail_at_write)
    {
        hk_result_t result = s_fail_result;
        s_fail_result = HK_OK;
        return result;
    }
    for(uint32_t source_pixel = 0U;
        source_pixel < size_bytes / 2U; source_pixel++)
    {
        uint32_t window_pixel = s_window_pixels + source_pixel;
        uint32_t x = (uint32_t)s_window.x +
            window_pixel % s_window.width;
        uint32_t y = (uint32_t)s_window.y +
            window_pixel / s_window.width;
        uint32_t destination = (y * TEST_WIDTH + x) * 2U;
        s_panel[destination] = pixels[source_pixel * 2U];
        s_panel[destination + 1U] = pixels[source_pixel * 2U + 1U];
    }
    s_window_pixels += (uint32_t)size_bytes / 2U;
    s_transfer_bytes += size_bytes;
    s_write_calls++;
    s_now_us += 10U;
    return HK_OK;
}

hk_result_t lcd_st7789_transport_end(
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(!s_stream_active)
        return HK_ERR_INVALID_STATE;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us && s_now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    s_stream_active = 0U;
    s_end_calls++;
    return HK_OK;
}

void lcd_st7789_transport_abort(void)
{
    if(!s_stream_active)
        return;
    s_stream_active = 0U;
    s_abort_calls++;
}

uint8_t *lcd_st7789_transport_shadow(void) { return s_shadow; }
uint32_t lcd_st7789_transport_shadow_size(void) { return sizeof(s_shadow); }
uint32_t lcd_st7789_transport_stride(void) { return TEST_WIDTH * 2U; }
uint16_t lcd_st7789_transport_shadow_pixel(uint16_t x, uint16_t y)
{
    uint32_t offset = ((uint32_t)y * TEST_WIDTH + x) * 2U;
    return (uint16_t)((uint16_t)s_shadow[offset] << 8) |
           s_shadow[offset + 1U];
}

static uint8_t cancel_probe(const void *context)
{
    cancel_fixture_t *fixture = (cancel_fixture_t *)context;
    return (uint8_t)(fixture->probes++ >= fixture->cancel_after);
}

static void fixture_reset(void)
{
    memset(s_shadow, 0, sizeof(s_shadow));
    memset(s_panel, 0, sizeof(s_panel));
    s_now_us = 0U;
    s_transfer_bytes = 0U;
    s_write_calls = 0U;
    s_begin_calls = 0U;
    s_end_calls = 0U;
    s_abort_calls = 0U;
    s_stream_active = 0U;
    s_window = (hk_display_rect_t){0};
    s_window_pixels = 0U;
    s_fail_result = HK_OK;
    s_fail_at_write = 0U;
    hk_k210_display_test_reset();
}

static void fixture_fail(hk_result_t result, uint32_t after_slices)
{
    s_fail_result = result;
    s_fail_at_write = s_write_calls + after_slices;
}

static uint64_t fixture_transferred_bytes(void)
{
    return s_transfer_bytes;
}

static uint16_t fixture_panel_pixel(uint32_t x, uint32_t y)
{
    uint32_t offset = (y * TEST_WIDTH + x) * 2U;
    return (uint16_t)((uint16_t)s_panel[offset] << 8) |
           s_panel[offset + 1U];
}


static void test_public_info_ownership_and_capacity(void)
{
    hk_display_t base = {0};
    hk_display_t overlay = {0};
    hk_display_t other = {0};
    hk_display_info_t info;
    hk_display_rect_t pixel = {0, 0, 1U, 1U};

    fixture_reset();
    require_true(hk_display_open(hk_display_service(), HK_DISPLAY_PLANE_BASE, &base) == HK_OK &&
                 hk_display_open(hk_display_service(), HK_DISPLAY_PLANE_OVERLAY,
                     &overlay) == HK_OK &&
                 hk_display_open(hk_display_service(), HK_DISPLAY_PLANE_BASE,
                     &other) == HK_ERR_BUSY,
                 "public BASE and OVERLAY ownership must be independent");
    require_true(hk_display_get_info(&base, &info) == HK_OK &&
                 info.width == TEST_WIDTH && info.height == TEST_HEIGHT &&
                 info.maximum_commands == 32U &&
                 info.transfer_slice_bytes == 128U &&
                 info.maximum_present_duration_us == 500000U,
                 "public info must publish bounded physical limits");
    require_true(hk_display_begin_batch(&base) == HK_OK,
                 "capacity batch must begin");
    for(uint16_t index = 0U; index < info.maximum_commands; index++)
        require_true(hk_display_fill_rect(&base, &pixel, index) == HK_OK,
                     "adapter must accept its advertised command capacity");
    require_true(hk_display_fill_rect(&base, &pixel, 0U) == HK_ERR_LIMIT &&
                 hk_display_abort(&base) == HK_OK,
                 "command overflow must be explicit and abortable");
    require_true(hk_display_close(&overlay, HK_DEADLINE_IMMEDIATE) == HK_OK &&
                 hk_display_close(&base, HK_DEADLINE_IMMEDIATE) == HK_OK,
                 "clean public releases must succeed");
}

static void test_public_overlay_cancel_retry_release(void)
{
    hk_display_t overlay = {0};
    cancel_fixture_t cancel_state = {0U, 8U};
    hk_cancel_t cancel = {cancel_probe, &cancel_state};
    uint64_t cancelled_bytes;

    fixture_reset();
    require_true(hk_display_open(hk_display_service(), HK_DISPLAY_PLANE_OVERLAY,
                     &overlay) == HK_OK &&
                 hk_display_begin_batch(&overlay) == HK_OK &&
                 hk_display_clear(&overlay, 0x001FU) == HK_OK,
                 "overlay retained batch must begin through public API");
    require_true(hk_display_present(&overlay, (hk_deadline_t){1000000U},
                     &cancel) == HK_ERR_CANCELLED,
                 "cancel must retain the unchanged overlay stage");
    cancelled_bytes = s_transfer_bytes;
    require_true(cancelled_bytes > 0U &&
                 hk_display_present(&overlay, (hk_deadline_t){1000000U},
                     NULL) == HK_OK,
                 "overlay retry must repair and commit");
    require_true(hk_display_close(&overlay, (hk_deadline_t){1000000U}) == HK_OK,
                 "overlay release must restore current BASE");
}

int main(void)
{
    const hk_display_normative_fixture_t normative = {
        "k210", fixture_reset, fixture_fail,
        fixture_transferred_bytes, fixture_panel_pixel,
    };

    require_true(hk_display_run_normative_suite(&normative) == 0,
                 "K210 must pass the shared public normative suite");
    test_public_info_ownership_and_capacity();
    test_public_overlay_cancel_retry_release();
    require_true(s_begin_calls > 0U && s_write_calls > 0U,
                 "adapter suite must exercise the raw panel transport");
    require_true(!s_stream_active &&
                 s_begin_calls == s_end_calls + s_abort_calls,
                 "every panel stream must end or abort exactly once");
    puts("K210_DISPLAY_ADAPTER_OK cases=10 normative=8 public_api=1 "
         "slice=128 framebuffer=153600 stream=one-per-region");
    return 0;
}
