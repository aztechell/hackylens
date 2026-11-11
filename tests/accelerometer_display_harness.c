/* Exercise the real K210 command limit and framebuffer transaction. */
#define TEST_WIDTH 240U
#define main display_contract_main
#include "k210_display_harness.c"
#undef main

#include "../firmware/src/app_runtime/surface_private.h"
#include "../firmware/src/apps/accelerometer/accelerometer_app.h"
#include <hackylens/app/accelerometer.h>

static hk_display_t display;
static hk_display_info_t display_info;
static hk_display_rect_t requested;
static unsigned sample_index;
static unsigned draw_commands;
static uint8_t full, locked;

uint8_t topbar_sd_mounted(void) { return 1U; }
void debug_console_write_text(const char *text) { (void)text; }

hk_result_t hk_app_context_state(const hk_app_context_t *ctx, void **data, uint32_t *bytes)
{
    (void)ctx;
    *data = accelerometer_v2_entry.state_storage;
    *bytes = accelerometer_v2_entry.state_capacity_bytes;
    return HK_OK;
}

hk_result_t hk_app_context_accelerometer(const hk_app_context_t *ctx, const hk_accelerometer_t **sensor)
{
    (void)ctx;
    *sensor = NULL;
    return HK_OK;
}

hk_result_t hk_app_context_request_render(const hk_app_context_t *ctx, const hk_display_rect_t *rect)
{
    (void)ctx;
    full = rect == NULL;
    if(rect)
        requested = *rect;
    return HK_OK;
}

hk_result_t hk_app_context_request_close(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}

hk_result_t hk_accelerometer_read(const hk_accelerometer_t *sensor, hk_acceleration_t *axes)
{
    (void)sensor;
    int phase = (int)(sample_index++ % 11U);
    *axes = (hk_acceleration_t){phase * 400 - 2000, 1800 - phase * 300,
                               phase % 2 ? -2000 : 1999};
    return HK_OK;
}

hk_result_t hk_accelerometer_read_motion(const hk_accelerometer_t *sensor,
                                         hk_acceleration_motion_t *motion)
{
    (void)sensor;
    *motion = (hk_acceleration_motion_t){
        .flags = HK_ACCEL_MOTION_TAP | HK_ACCEL_MOTION_DOUBLE_TAP |
                 HK_ACCEL_MOTION_ACTIVITY | HK_ACCEL_MOTION_ORIENTATION,
        .orientation = HK_ACCEL_LANDSCAPE_LEFT,
    };
    return HK_OK;
}

static hk_result_t invalidate(void *user, const hk_display_rect_t *rect)
{
    (void)user;
    const hk_display_rect_t screen = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    return hk_display_mark_dirty(&display, rect ? rect : &screen);
}

static hk_result_t clear(void *user, uint16_t color)
{
    (void)user;
    draw_commands++;
    return hk_display_clear(&display, color);
}

static hk_result_t fill(void *user, const hk_display_rect_t *rect, uint16_t color)
{
    (void)user;
    draw_commands++;
    return hk_display_fill_rect(&display, rect, color);
}

static hk_result_t stroke(void *user, const hk_display_rect_t *rect, uint16_t color)
{
    (void)user;
    draw_commands++;
    return hk_display_stroke_rect(&display, rect, color);
}

static hk_result_t text(void *user, const hk_display_rect_t *rect, const char *value,
                         uint32_t bytes, uint16_t color)
{
    (void)user;
    draw_commands++;
    return hk_display_text(&display, rect, value, bytes, color);
}

static hk_result_t blit(void *user, const hk_display_rect_t *rect,
                         const hk_buffer_view_t *view, uint32_t format)
{
    (void)user;
    return hk_display_blit(&display, rect, view, format);
}

static hk_result_t lock(void *user, hk_display_surface_t *pixels)
{
    (void)user;
    require_true(!locked && !draw_commands, "lock before drawing");
    require_true(hk_display_abort(&display) == HK_OK, "retire empty batch");
    locked = 1U;
    return hk_display_surface_acquire(&display, pixels);
}

static void render(uint8_t trace)
{
    const hk_app_surface_ops_t ops = {.invalidate = invalidate, .clear = clear,
        .fill_rect = fill, .stroke_rect = stroke, .text = text, .blit = blit, .lock = lock};
    hk_app_surface_t surface;
    require_true(hk_display_begin_batch(&display) == HK_OK, "start render transaction");
    require_true(hk_app_surface_private_init(&surface, 1U, &display_info, &ops) == HK_OK,
                 "initialize app surface");
    require_true(hk_app_surface_invalidate(&surface, full ? NULL : &requested) == HK_OK,
                 "apply requested invalidation");
    draw_commands = locked = 0U;
    hk_result_t result = accelerometer_v2_entry.render(NULL, &surface);
    if(result != HK_OK)
        fprintf(stderr, "render result=%d commands=%u samples=%u\n",
                (int)result, draw_commands, sample_index);
    require_true(result == HK_OK && surface.invalidated, "render succeeded");
    require_true(trace ? locked && !draw_commands : !locked, "expected render path");
    result = hk_display_present(&display, (hk_deadline_t){s_now_us + 500000U}, NULL);
    if(result != HK_OK)
        fprintf(stderr, "present result=%d samples=%u\n", (int)result, sample_index);
    require_true(result == HK_OK, "present app frame");
}

static void input(uint32_t pressed)
{
    hk_app_event_t event = {.kind = HK_APP_EVENT_INPUT};
    event.data.input.pressed = pressed;
    require_true(accelerometer_v2_entry.event(NULL, &event) == HK_OK, "input accepted");
}

int main(void)
{
    const hk_app_event_t tick = {.kind = HK_APP_EVENT_TIMER};
    fixture_reset();
    require_true(hk_display_open(hk_display_service(), HK_DISPLAY_PLANE_BASE, &display) == HK_OK,
                 "open base plane");
    require_true(hk_display_get_info(&display, &display_info) == HK_OK, "read display info");
    require_true(accelerometer_v2_entry.start(NULL) == HK_OK, "start app");
    full = 1U;
    render(0U);

    /* Enter TRACE with full history, then refresh through multiple wraps. */
    for(unsigned i = 0U; i < 48U; i++)
        require_true(accelerometer_v2_entry.event(NULL, &tick) == HK_OK, "sample while in level");
    input(HK_INPUT_BUTTON_RIGHT);
    render(1U);
    require_true(fixture_panel_pixel(229U, 163U) != 0U, "trace axis visible");
    uint8_t footer_visible = 0U;
    for(unsigned y = 208U; y < 232U; y++)
        for(unsigned x = 12U; x < TEST_WIDTH - 12U; x++)
            footer_visible |= fixture_panel_pixel(x, y) == 0x07e0U;
    require_true(footer_visible, "trace scale and duration visible");
    for(unsigned i = 0U; i < 100U; i++)
    {
        require_true(accelerometer_v2_entry.event(NULL, &tick) == HK_OK, "sample while tracing");
        render(1U);
    }
    input(HK_INPUT_BUTTON_RIGHT);
    render(0U);
    input(HK_INPUT_BUTTON_LEFT);
    render(1U);
    input(HK_INPUT_BUTTON_LEFT);
    render(0U);
    require_true(accelerometer_v2_entry.stop(NULL) == HK_OK, "stop app");
    require_true(hk_display_close(&display, (hk_deadline_t){s_now_us + 500000U}) == HK_OK,
                 "close base plane");
    puts("ACCELEROMETER_DISPLAY_OK");
    return 0;
}
