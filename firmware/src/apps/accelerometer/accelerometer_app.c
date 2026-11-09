#include "accelerometer_app.h"

#include <stddef.h>
#include <stdio.h>

#include <hackylens/app/accelerometer.h>
#include <hackylens/app/panel.h>

#define BODY_Y 38U
#define BODY_HEIGHT 186U
#define FULL_SCALE_CENTIG 200

typedef struct
{
    const hk_accelerometer_t *sensor;
    hk_result_t status;
    int16_t axes[3];
    uint16_t width;
    uint8_t full_redraw;
} accelerometer_state_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t storage[1024];
_Static_assert(sizeof(accelerometer_state_t) <= sizeof(storage), "Accelerometer state is too large");

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
    *state = (accelerometer_state_t){.status = HK_PENDING, .full_redraw = 1U};
    return hk_app_context_accelerometer(ctx, &state->sensor);
}

static int16_t centi_g(int16_t milli_g)
{
    return (int16_t)((milli_g + (milli_g < 0 ? -5 : 5)) / 10);
}

static hk_result_t accelerometer_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    if(event->kind == HK_APP_EVENT_INPUT && (event->data.input.pressed & HK_INPUT_BUTTON_BACK))
        return hk_app_context_request_close(ctx);
    if(event->kind != HK_APP_EVENT_TIMER)
        return HK_OK;
    accelerometer_state_t *state = app_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;

    hk_acceleration_t sample;
    hk_result_t result = hk_accelerometer_read(state->sensor, &sample);
    uint8_t changed = state->status != result;
    state->status = result;
    if(result == HK_OK)
    {
        const int16_t axes[] = {centi_g(sample.x_mg), centi_g(sample.y_mg), centi_g(sample.z_mg)};
        for(uint8_t i = 0U; i < 3U; i++)
        {
            changed |= state->axes[i] != axes[i];
            state->axes[i] = axes[i];
        }
    }
    if(!changed)
        return HK_OK;
    const hk_display_rect_t body = {2, BODY_Y, state->width > 4U ? state->width - 4U : 1U,
                                    BODY_HEIGHT};
    return hk_app_context_request_render(ctx, state->full_redraw ? NULL : &body);
}

static void draw_axis(hk_app_panel_t *panel, const char *axis, uint16_t y, int16_t value)
{
    char label[16];
    unsigned magnitude = (unsigned)(value < 0 ? -value : value);
    (void)snprintf(label, sizeof(label), "%c%u.%02u g", value < 0 ? '-' : '+',
                   magnitude / 100U, magnitude % 100U);
    uint16_t width = panel->width - 24U;
    hk_app_panel_label(panel, 12U, y, width, axis, HK_PANEL_GREEN, HK_PANEL_LEFT);
    hk_app_panel_label(panel, 12U, y, width, label, HK_PANEL_GREEN, HK_PANEL_RIGHT);
    y += 28U;
    hk_app_panel_fill(panel, 12U, y, width, 8U, HK_PANEL_TRACK);
    uint16_t center = 12U + width / 2U;
    unsigned clamped = magnitude < FULL_SCALE_CENTIG ? magnitude : FULL_SCALE_CENTIG;
    uint16_t span = (uint16_t)((width / 2U) * clamped / FULL_SCALE_CENTIG);
    hk_app_panel_fill(panel, value < 0 ? center - span : center, y, span, 8U, HK_PANEL_GREEN);
    hk_app_panel_fill(panel, center - 1U, y - 2U, 2U, 12U, HK_PANEL_GREEN);
}

static hk_result_t accelerometer_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    accelerometer_state_t *state = app_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    if(panel.result != HK_OK)
        return panel.result;
    if(state->full_redraw)
        hk_app_panel_chrome(&panel, "ACCELEROMETER");
    else
        hk_app_panel_fill(&panel, 2U, BODY_Y, panel.width - 4U, BODY_HEIGHT, HK_PANEL_BLACK);
    if(state->status == HK_OK)
    {
        draw_axis(&panel, "X", 49U, state->axes[0]);
        draw_axis(&panel, "Y", 108U, state->axes[1]);
        draw_axis(&panel, "Z", 167U, state->axes[2]);
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
    return HK_OK;
}

const hk_app_v2_entry_t accelerometer_v2_entry = {
    .state_storage = storage,
    .state_capacity_bytes = sizeof(storage),
    .start = accelerometer_start,
    .event = accelerometer_event,
    .render = accelerometer_render,
    .stop = accelerometer_stop,
};
