#include "accelerometer_app.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/app/accelerometer.h>
#include <hackylens/app/panel.h>

#include "../../config/menu_layout.h"
#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "../../ui/hk_font.h"
#include "../../ui/hk_ui.h"
#include "hackylens_font_1bpp.h"

#define BODY_Y 70U
#define BODY_HEIGHT 164U
#define TRACE_SAMPLES 48U
#define FULL_SCALE_MG 2000

typedef enum { LEVEL, TRACE, EVENTS, PAGE_COUNT } page_t;

static const char *const page_names[] = {"LEVEL", "TRACE", "EVENTS"};
static const uint16_t axis_colors[] = {HK_PANEL_GREEN, 0x07ffU, 0xffe0U};

typedef struct
{
    const hk_accelerometer_t *sensor;
    hk_result_t status;
    hk_result_t motion_status;
    hk_acceleration_motion_t motion;
    int16_t gravity[3];
    int16_t history[TRACE_SAMPLES][3];
    uint16_t width;
    page_t page;
    uint8_t next_sample;
    uint8_t count;
    uint8_t seen;
    uint8_t orientation_valid;
    uint8_t full_redraw;
} accelerometer_state_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t storage[1024];
_Static_assert(sizeof(accelerometer_state_t) <= sizeof(storage), "Accelerometer state is too large");
static const accelerometer_state_t *active;

static accelerometer_state_t *app_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t size = 0U;
    return hk_app_context_state(ctx, &state, &size) == HK_OK &&
           size >= sizeof(accelerometer_state_t) ? state : NULL;
}

static hk_result_t accelerometer_start(const hk_app_context_t *ctx)
{
    accelerometer_state_t *state = app_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;
    *state = (accelerometer_state_t){.status = HK_PENDING, .motion_status = HK_PENDING,
                                    .full_redraw = 1U};
    hk_result_t result = hk_app_context_accelerometer(ctx, &state->sensor);
    if(result == HK_OK)
        active = state;
    return result;
}

static void capture_sample(accelerometer_state_t *state)
{
    hk_acceleration_t sample;
    state->status = hk_accelerometer_read(state->sensor, &sample);
    if(state->status != HK_OK)
    {
        state->count = 0U;
        state->orientation_valid = 0U;
        return;
    }

    const int16_t axes[] = {sample.x_mg, sample.y_mg, sample.z_mg};
    for(uint8_t i = 0U; i < 3U; i++)
    {
        state->history[state->next_sample][i] = axes[i];
        state->gravity[i] = state->count ? (int16_t)((2 * state->gravity[i] + axes[i]) / 3) : axes[i];
    }
    state->next_sample = (state->next_sample + 1U) % TRACE_SAMPLES;
    if(state->count < TRACE_SAMPLES)
        state->count++;

    uint8_t previous_flags = state->motion_status == HK_OK ? state->motion.flags : 0U;
    state->motion_status = hk_accelerometer_read_motion(state->sensor, &state->motion);
    if(state->motion_status == HK_OK)
    {
        state->seen |= state->motion.flags & (uint8_t)~previous_flags;
        if(state->motion.flags & HK_ACCEL_MOTION_ORIENTATION)
            state->orientation_valid = 1U;
    }
    else
        state->orientation_valid = 0U;
}

static hk_result_t accelerometer_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    accelerometer_state_t *state = app_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;

    if(event->kind == HK_APP_EVENT_INPUT)
    {
        uint32_t pressed = event->data.input.pressed;
        if(pressed & HK_INPUT_BUTTON_BACK)
            return hk_app_context_request_close(ctx);
        if(pressed & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_RIGHT))
        {
            unsigned step = pressed & HK_INPUT_BUTTON_RIGHT ? 1U : PAGE_COUNT - 1U;
            state->page = (page_t)((state->page + step) % PAGE_COUNT);
            state->full_redraw = 1U;
            return hk_app_context_request_render(ctx, NULL);
        }
        if((pressed & HK_INPUT_BUTTON_OK) && state->page == EVENTS)
            state->seen = 0U;
        else
            return HK_OK;
    }
    else if(event->kind == HK_APP_EVENT_TIMER)
        capture_sample(state);
    else
        return HK_OK;

    const hk_display_rect_t body = {2, BODY_Y, state->width > 4U ? state->width - 4U : 1U,
                                    BODY_HEIGHT};
    return hk_app_context_request_render(ctx, state->full_redraw ? NULL : &body);
}

static hk_display_rect_t tab_bounds(uint16_t width, page_t page)
{
    uint16_t usable = width - 4U;
    uint16_t column = usable * 5U / 16U;
    return (hk_display_rect_t){2U + page * column, 37U,
                              page == EVENTS ? usable - 2U * column : column, 28U};
}

static void draw_tabs(hk_app_panel_t *panel, page_t page)
{
    for(uint8_t i = 0U; i < PAGE_COUNT; i++)
    {
        hk_display_rect_t tab = tab_bounds(panel->width, (page_t)i);
        if(i == page)
            hk_app_panel_fill(panel, tab.x, tab.y, tab.width, tab.height, HK_PANEL_GREEN);
        hk_app_panel_label(panel, tab.x, tab.y + 2U, tab.width, page_names[i],
                           i == page ? HK_PANEL_BLACK : HK_PANEL_GREEN, HK_PANEL_CENTER);
    }
}

static int tilt_degrees(int32_t axis, int32_t cross_a, int32_t cross_b)
{
    float angle = atan2f((float)axis, sqrtf((float)(cross_a * cross_a + cross_b * cross_b)));
    float degrees = angle * 57.29578f;
    return (int)(degrees + (degrees < 0.0f ? -0.5f : 0.5f));
}

static void draw_level(hk_app_panel_t *panel, const int16_t gravity[3])
{
    int32_t x = gravity[0], y = gravity[1], z = gravity[2];
    int32_t length_squared = x * x + y * y + z * z;
    if(length_squared < 750 * 750 || length_squared > 1250 * 1250)
    {
        hk_app_panel_status(panel, 116U, "HOLD STILL");
        return;
    }

    uint16_t center = panel->width / 2U;
    const uint16_t middle = 133U;
    const hk_display_rect_t border = {center - 49U, middle - 49U, 99U, 99U};
    if(panel->result == HK_OK)
        panel->result = hk_app_surface_stroke_rect(panel->surface, &border, HK_PANEL_TRACK);
    hk_app_panel_fill(panel, center - 48U, middle, 97U, 1U, HK_PANEL_TRACK);
    hk_app_panel_fill(panel, center, middle - 48U, 1U, 97U, HK_PANEL_TRACK);
    const hk_display_rect_t target = {center - 6U, middle - 6U, 13U, 13U};
    if(panel->result == HK_OK)
        panel->result = hk_app_surface_stroke_rect(panel->surface, &target, HK_PANEL_GREEN);
    uint16_t px = (uint16_t)(center + x * 44 / 1250);
    uint16_t py = (uint16_t)(middle - y * 44 / 1250);
    hk_app_panel_fill(panel, px - 4U, py - 2U, 9U, 5U, HK_PANEL_GREEN);
    hk_app_panel_fill(panel, px - 2U, py - 4U, 5U, 9U, HK_PANEL_GREEN);
    char angles[28];
    (void)snprintf(angles, sizeof(angles), "X%+d Y%+d DEG",
                   tilt_degrees(x, y, z), tilt_degrees(y, x, z));
    hk_app_panel_status(panel, 192U, angles);
}

/* TRACE draws into the display provider's existing RGB565 backing store.
 * Each segment can cover several pixels without consuming a batch command. */
static void raster_fill(hk_display_surface_t *pixels, unsigned x, unsigned y,
                        unsigned width, unsigned height, uint16_t color)
{
    for(unsigned row = 0U; row < height; row++)
    {
        uint8_t *out = (uint8_t *)pixels->pixels.data +
                       (y + row) * pixels->pixels.stride_bytes + x * 2U;
        for(unsigned column = 0U; column < width; column++)
        {
            *out++ = (uint8_t)(color >> 8U);
            *out++ = (uint8_t)color;
        }
    }
}

static void raster_label(hk_display_surface_t *pixels, unsigned x, unsigned y,
                         unsigned width, const char *text, uint16_t color)
{
    size_t length = strlen(text);
    if(length * HACKYLENS_FONT_W > width)
        return;
    x += (width - length * HACKYLENS_FONT_W) / 2U;
    for(size_t i = 0U; i < length; i++)
    {
        const uint8_t *glyph = hk_font_glyph((uint8_t)text[i]);
        for(unsigned row = 0U; row < HACKYLENS_FONT_H; row++)
            for(unsigned col = 0U; col < HACKYLENS_FONT_W; col++)
                if(glyph[row * HACKYLENS_FONT_ROW_BYTES + col / 8U] & (0x80U >> (col % 8U)))
                    raster_fill(pixels, x + i * HACKYLENS_FONT_W + col, y + row, 1U, 1U, color);
    }
}

static void trace_header(hk_display_surface_t *pixels)
{
    raster_fill(pixels, 0U, 0U, pixels->width, pixels->height, HK_PANEL_GREEN);
    raster_fill(pixels, 2U, 2U, pixels->width - 4U, pixels->height - 4U, HK_PANEL_BLACK);
    raster_fill(pixels, 0U, MENU_BAR_H - MENU_LINE, pixels->width, MENU_LINE, HK_PANEL_GREEN);
    raster_label(pixels, 24U, 3U, pixels->width - 48U, "ACCELEROMETER", HK_PANEL_GREEN);
    if(topbar_sd_mounted())
    {
        unsigned x = pixels->width - 23U;
        raster_fill(pixels, x + 2U, 7U, 10U, 2U, HK_PANEL_GREEN);
        raster_fill(pixels, x + 12U, 9U, 2U, 10U, HK_PANEL_GREEN);
        raster_fill(pixels, x, 10U, 2U, 11U, HK_PANEL_GREEN);
        raster_fill(pixels, x + 2U, 19U, 12U, 2U, HK_PANEL_GREEN);
        raster_fill(pixels, x + 9U, 7U, 2U, 5U, HK_PANEL_GREEN);
        for(unsigned i = 0U; i < 4U; i++)
            raster_fill(pixels, x + 3U + i * 2U, 16U, 2U, 3U, HK_PANEL_GREEN);
    }
    for(uint8_t i = 0U; i < PAGE_COUNT; i++)
    {
        hk_display_rect_t tab = tab_bounds((uint16_t)pixels->width, (page_t)i);
        if(i == TRACE)
            raster_fill(pixels, tab.x, tab.y, tab.width, tab.height, HK_PANEL_GREEN);
        raster_label(pixels, tab.x, tab.y + 2U, tab.width, page_names[i],
                     i == TRACE ? HK_PANEL_BLACK : HK_PANEL_GREEN);
    }
}

static void draw_trace(hk_display_surface_t *pixels, const accelerometer_state_t *state)
{
    uint16_t width = (pixels->width - 16U) / 3U;
    uint8_t latest = (state->next_sample + TRACE_SAMPLES - 1U) % TRACE_SAMPLES;
    const uint16_t origin = 10U, right = pixels->width - 10U, middle = 163U;
    raster_fill(pixels, origin, middle, right - origin, 1U, HK_PANEL_TRACK);
    for(uint8_t axis = 0U; axis < 3U; axis++)
    {
        int value = state->history[latest][axis];
        int centi = (value + (value < 0 ? -5 : 5)) / 10;
        unsigned magnitude = (unsigned)(centi < 0 ? -centi : centi);
        char label[12];
        (void)snprintf(label, sizeof(label), "%c%u.%02u",
                       centi < 0 ? '-' : '+', magnitude / 100U, magnitude % 100U);
        uint16_t color = axis_colors[axis];
        char axis_name[] = {"XYZ"[axis], '\0'};
        raster_label(pixels, 8U + axis * width, 70U, width, axis_name, color);
        raster_label(pixels, 8U + axis * width, 94U, width, label, color);
        uint16_t previous_y = middle, previous_x = origin;
        uint8_t first = (state->next_sample + TRACE_SAMPLES - state->count) % TRACE_SAMPLES;
        for(uint8_t i = 0U; i < state->count; i++)
        {
            uint8_t index = (first + i) % TRACE_SAMPLES;
            uint16_t px = right - (state->count - 1U - i) * (right - origin) / (TRACE_SAMPLES - 1U);
            int value_mg = state->history[index][axis];
            if(value_mg < -FULL_SCALE_MG) value_mg = -FULL_SCALE_MG;
            if(value_mg > FULL_SCALE_MG) value_mg = FULL_SCALE_MG;
            uint16_t py = (uint16_t)(middle - value_mg * 39 / FULL_SCALE_MG);
            if(i)
            {
                uint16_t top = py < previous_y ? py : previous_y;
                uint16_t bottom = py > previous_y ? py : previous_y;
                raster_fill(pixels, previous_x, previous_y, px - previous_x, 1U, color);
                raster_fill(pixels, px, top, 1U, bottom - top + 1U, color);
            }
            else
                raster_fill(pixels, px, py, 1U, 1U, color);
            previous_x = px;
            previous_y = py;
        }
    }
    raster_label(pixels, 12U, 208U, pixels->width - 24U, "+/-2G / 5 SEC", HK_PANEL_GREEN);
}

static hk_result_t render_trace(hk_app_surface_t *surface, accelerometer_state_t *state)
{
    hk_display_surface_t pixels;
    hk_result_t result = hk_app_surface_lock(surface, &pixels);
    if(result != HK_OK)
        return result;
    if(pixels.pixel_format != HK_DISPLAY_FORMAT_RGB565_BE || !pixels.pixels.data ||
       !(pixels.pixels.flags & HK_BUFFER_ACCESS_WRITABLE) ||
       pixels.width < 240U || pixels.width > UINT16_MAX ||
       pixels.height < 240U || pixels.pixels.stride_bytes < pixels.width * 2U ||
       (uint64_t)pixels.pixels.stride_bytes * pixels.height > pixels.pixels.size_bytes)
        return HK_ERR_INVALID_ARGUMENT;

    const hk_display_rect_t body = {2U, BODY_Y, pixels.width - 4U, BODY_HEIGHT};
    if(state->full_redraw)
        trace_header(&pixels);
    else
        raster_fill(&pixels, body.x, body.y, body.width, body.height, HK_PANEL_BLACK);
    if(state->status == HK_OK)
        draw_trace(&pixels, state);
    else
        raster_label(&pixels, 12U, 106U, pixels.width - 24U,
                     state->status == HK_PENDING ? "STARTING" : "SENSOR ERROR", HK_PANEL_GREEN);
    result = hk_app_surface_invalidate(surface, state->full_redraw ? NULL : &body);
    if(result == HK_OK)
    {
        state->width = (uint16_t)pixels.width;
        state->full_redraw = 0U;
    }
    return result;
}

static void draw_events(hk_app_panel_t *panel, const accelerometer_state_t *state)
{
    static const char *const poses[] = {"PORTRAIT", "INVERTED", "LANDSCAPE L", "LANDSCAPE R"};
    static const char *const names[] = {"TAP", "DOUBLE TAP", "MOTION", "FREE FALL"};
    static const uint8_t flags[] = {HK_ACCEL_MOTION_TAP, HK_ACCEL_MOTION_DOUBLE_TAP,
                                   HK_ACCEL_MOTION_ACTIVITY, HK_ACCEL_MOTION_FREE_FALL};
    if(state->motion_status != HK_OK)
    {
        hk_app_panel_status(panel, 113U, "EVENTS UNAVAILABLE");
        return;
    }

    if(state->orientation_valid)
    {
        hk_app_panel_label(panel, 12U, 74U, panel->width - 24U,
                           poses[state->motion.orientation], HK_PANEL_GREEN, HK_PANEL_LEFT);
        hk_app_panel_label(panel, 12U, 74U, panel->width - 24U,
                           state->motion.face_down ? "DOWN" : "UP", HK_PANEL_GREEN, HK_PANEL_RIGHT);
    }
    else
        hk_app_panel_status(panel, 74U, "TURN CUBE");
    for(uint8_t i = 0U; i < 4U; i++)
    {
        uint16_t y = 103U + i * 26U;
        uint8_t live = state->motion.flags & flags[i];
        uint16_t color = live ? HK_PANEL_BLACK : HK_PANEL_GREEN;
        if(live)
            hk_app_panel_fill(panel, 8U, y, panel->width - 16U, 25U, HK_PANEL_GREEN);
        hk_app_panel_label(panel, 12U, y, panel->width - 24U, names[i], color, HK_PANEL_LEFT);
        hk_app_panel_label(panel, 12U, y, panel->width - 24U,
                           live ? "LIVE" : (state->seen & flags[i] ? "SEEN" : "--"),
                           color, HK_PANEL_RIGHT);
    }
    hk_app_panel_status(panel, 208U, "OK CLEAR");
}

static hk_result_t accelerometer_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    accelerometer_state_t *state = app_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;
    if(state->page == TRACE)
        return render_trace(surface, state);
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    if(panel.result != HK_OK)
        return panel.result;
    if(state->full_redraw)
    {
        hk_app_panel_chrome(&panel, "ACCELEROMETER");
        draw_tabs(&panel, state->page);
    }
    else
        hk_app_panel_fill(&panel, 2U, BODY_Y, panel.width - 4U, BODY_HEIGHT, HK_PANEL_BLACK);
    if(state->status == HK_OK)
    {
        if(state->page == LEVEL)
            draw_level(&panel, state->gravity);
        else
            draw_events(&panel, state);
    }
    else
        hk_app_panel_status(&panel, 106U, state->status == HK_PENDING ? "STARTING" : "SENSOR ERROR");
    if(panel.result == HK_OK)
    {
        state->width = panel.width;
        state->full_redraw = 0U;
    }
    return panel.result;
}

static hk_result_t accelerometer_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    active = NULL;
    return HK_OK;
}

uint8_t accelerometer_handle_debug_command(const char *command)
{
    if(!str_eq_ci(command, "HKACCELINFO"))
        return 0U;
    if(!active)
        debug_console_write_text("HKACCELINFO active=0\r\n");
    else
    {
        uint8_t latest = (active->next_sample + TRACE_SAMPLES - 1U) % TRACE_SAMPLES;
        const int16_t *axes = active->history[latest];
        printf("HKACCELINFO active=1 page=%s status=%d x=%d y=%d z=%d events=%d "
               "flags=%02x seen=%02x orientation=%u valid=%u down=%u\r\n",
               page_names[active->page], (int)active->status, axes[0], axes[1], axes[2],
               (int)active->motion_status, (unsigned)active->motion.flags,
               (unsigned)active->seen, (unsigned)active->motion.orientation,
               (unsigned)active->orientation_valid, (unsigned)active->motion.face_down);
    }
    return 1U;
}

const hk_app_v2_entry_t accelerometer_v2_entry = {
    .state_storage = storage,
    .state_capacity_bytes = sizeof(storage),
    .start = accelerometer_start,
    .event = accelerometer_event,
    .render = accelerometer_render,
    .stop = accelerometer_stop,
};
