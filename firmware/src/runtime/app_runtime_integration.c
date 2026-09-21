#include "app_runtime_integration.h"

#include <string.h>

#include <hackylens/capability/external_link.h>
#include <hackylens/capability/lights.h>

#include "../app_runtime/surface_private.h"
#include "../ui/display_binding.h"
#include "../services/camera_light.h"

typedef struct
{
    hk_app_switch_t switcher;
    const hk_time_t *time;
    hk_display_t *display;
    hk_display_info_t display_info;
    hk_display_surface_t locked_surface;
    uint8_t initialized;
    uint8_t display_batch_active;
    uint8_t display_surface_active;
} app_runtime_integration_t;

static app_runtime_integration_t s_integration;

static hk_result_t render_abort(void *user);

static hk_result_t prepare(void *user, const hk_app_t *app)
{
    app_runtime_integration_t *integration = user;
    hk_result_t result;

    (void)app;
    integration->display = &integration->switcher.runtime.display[0];
    result = hk_display_open(integration->switcher.runtime.ops.display,
        HK_DISPLAY_PLANE_BASE, integration->display);
    if(result == HK_OK)
        result = hk_ui_display_bind(integration->display);
    return result;
}

static hk_result_t cleanup(void *user, hk_deadline_t deadline)
{
    app_runtime_integration_t *integration = user;
    hk_result_t result = camera_light_retire(deadline);

    hk_ui_display_unbind();
    integration->display = NULL;
    integration->display_batch_active = 0U;
    integration->display_surface_active = 0U;
    integration->locked_surface = (hk_display_surface_t){0};
    return result;
}

static hk_result_t now_us(void *user, uint64_t *value)
{
    app_runtime_integration_t *integration = user;

    return hk_time_now_us(
        integration->time, value);
}

static hk_result_t deadline_after_us(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    app_runtime_integration_t *integration = user;

    return hk_time_deadline_after_us(
        integration->time,
        duration_us, deadline);
}

static hk_result_t surface_invalidate(
    void *user,
    const hk_display_rect_t *region)
{
    app_runtime_integration_t *integration = user;
    hk_display_rect_t full;

    if(!region)
    {
        full = (hk_display_rect_t){
            0, 0,
            integration->display_info.width,
            integration->display_info.height,
        };
        region = &full;
    }
    return hk_display_mark_dirty(
        integration->display, region);
}

static hk_result_t surface_clear(void *user, uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;

    if(integration->display_surface_active)
        return HK_ERR_INVALID_STATE;
    return hk_display_clear(
        integration->display, rgb565);
}

static hk_result_t surface_fill_rect(
    void *user,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;

    if(integration->display_surface_active)
        return HK_ERR_INVALID_STATE;
    return hk_display_fill_rect(
        integration->display, rect, rgb565);
}

static hk_result_t surface_stroke_rect(
    void *user,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;

    if(integration->display_surface_active)
        return HK_ERR_INVALID_STATE;
    return hk_display_stroke_rect(
        integration->display, rect, rgb565);
}

static hk_result_t surface_text(
    void *user,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;

    if(integration->display_surface_active)
        return HK_ERR_INVALID_STATE;
    return hk_display_text(
        integration->display, bounds, utf8, size_bytes, rgb565);
}

static hk_result_t surface_blit(
    void *user,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    app_runtime_integration_t *integration = user;

    if(integration->display_surface_active)
        return HK_ERR_INVALID_STATE;
    return hk_display_blit(
        integration->display, destination, pixels, pixel_format);
}

static hk_result_t surface_lock(void *user, hk_display_surface_t *pixels)
{
    app_runtime_integration_t *integration = user;
    hk_result_t result;

    if(!pixels)
        return HK_ERR_INVALID_ARGUMENT;
    if(integration->display_surface_active)
    {
        *pixels = integration->locked_surface;
        return HK_OK;
    }
    if(integration->display_batch_active)
    {
        result = hk_display_abort(
            integration->display);
        if(result != HK_OK)
            return result;
        integration->display_batch_active = 0U;
    }
    result = hk_display_surface_acquire(
        integration->display, pixels);
    if(result != HK_OK)
        return result;
    integration->locked_surface = *pixels;
    integration->display_surface_active = 1U;
    return HK_OK;
}

static hk_result_t render_begin(
    void *user,
    const hk_app_runtime_t *runtime,
    hk_app_surface_t *surface)
{
    static const hk_app_surface_ops_t surface_ops = {
        .invalidate = surface_invalidate,
        .clear = surface_clear,
        .fill_rect = surface_fill_rect,
        .stroke_rect = surface_stroke_rect,
        .text = surface_text,
        .blit = surface_blit,
        .lock = surface_lock,
    };
    app_runtime_integration_t *integration = user;
    hk_result_t result;

    if(!integration->display || !integration->display->service)
        return HK_ERR_CAPABILITY_ABSENT;
    result = hk_display_get_info(
        integration->display,
        &integration->display_info);
    if(result == HK_OK)
        result = hk_display_begin_batch(integration->display);
    if(result != HK_OK)
        return result;
    integration->display_batch_active = 1U;
    {
        hk_app_surface_ops_t bound_ops = surface_ops;
        hk_result_t init_result;

        bound_ops.user = integration;
        init_result = hk_app_surface_private_init(
            surface, runtime->context_generation,
            &integration->display_info, &bound_ops);
        if(init_result != HK_OK)
            (void)render_abort(integration);
        return init_result;
    }
}

static hk_result_t render_present(void *user, hk_deadline_t deadline)
{
    app_runtime_integration_t *integration = user;
    hk_result_t result = hk_display_present(
        integration->display, deadline, NULL);

    if(result == HK_OK)
    {
        integration->display_batch_active = 0U;
        integration->display_surface_active = 0U;
        integration->locked_surface = (hk_display_surface_t){0};
    }
    return result;
}

static hk_result_t render_abort(void *user)
{
    app_runtime_integration_t *integration = user;
    hk_result_t result;

    if(!integration->display_batch_active &&
       !integration->display_surface_active)
        return HK_OK;
    result = hk_display_abort(
        integration->display);
    if(result == HK_OK)
    {
        integration->display_batch_active = 0U;
        integration->display_surface_active = 0U;
        integration->locked_surface = (hk_display_surface_t){0};
    }
    return result;
}

hk_result_t app_runtime_integration_initialize(void)
{
    static const hk_app_switch_ops_t switch_ops = {
        .user = &s_integration,
        .now_us = now_us,
        .render_begin = render_begin,
        .render_present = render_present,
        .render_abort = render_abort,
    };
    hk_app_runtime_ops_t runtime_ops;
    hk_result_t result;

    if(s_integration.initialized)
        return HK_OK;
    memset(&s_integration, 0, sizeof(s_integration));
    s_integration.time = hk_time_service();
    if(!s_integration.time)
        return HK_ERR_CAPABILITY_ABSENT;
    runtime_ops = (hk_app_runtime_ops_t){
        .user = &s_integration,
        .time = s_integration.time,
        .input = hk_input_service(),
        .lights = hk_lights_service(),
        .display = hk_display_service(),
        .external_link = hk_external_link_service(),
        .prepare = prepare,
        .cleanup = cleanup,
        .deadline_after_us = deadline_after_us,
    };
    result = hk_app_switch_init(
        &s_integration.switcher, &runtime_ops, &switch_ops,
        HK_APP_RUNTIME_TEARDOWN_BUDGET_US);
    if(result == HK_OK)
        s_integration.initialized = 1U;
    return result;
}

hk_result_t app_runtime_integration_open(
    const hk_app_t *app,
    const hk_input_snapshot_t *input)
{
    if(!s_integration.initialized)
        return HK_ERR_INVALID_STATE;
    return hk_app_switch_open(&s_integration.switcher, app, input);
}

hk_result_t app_runtime_integration_close(hk_app_stop_reason_t reason)
{
    if(!s_integration.initialized)
        return HK_ERR_INVALID_STATE;
    return hk_app_switch_close(&s_integration.switcher, reason);
}

hk_result_t app_runtime_integration_input(
    const hk_input_event_t *input,
    uint8_t *consumed)
{
    return hk_app_switch_input(&s_integration.switcher, input, consumed);
}

hk_result_t app_runtime_integration_media(
    hk_app_media_kind_t kind,
    uint32_t generation)
{
    uint64_t timestamp_us;
    hk_result_t result = app_runtime_integration_now_us(&timestamp_us);

    if(result != HK_OK)
        return result;
    return hk_app_switch_media(
        &s_integration.switcher, kind, generation, timestamp_us);
}

hk_result_t app_runtime_integration_wakeup(hk_app_wakeup_token_t token)
{
    uint64_t timestamp_us;
    hk_result_t result = app_runtime_integration_now_us(&timestamp_us);

    if(result != HK_OK)
        return result;
    return hk_app_switch_wakeup(
        &s_integration.switcher, token, timestamp_us);
}

hk_result_t app_runtime_integration_poll(uint64_t now_us)
{
    return hk_app_switch_poll(&s_integration.switcher, now_us);
}

hk_result_t app_runtime_integration_now_us(uint64_t *value)
{
    if(!s_integration.initialized || !value)
        return HK_ERR_INVALID_STATE;
    return now_us(&s_integration, value);
}

uint32_t app_runtime_integration_poll_interval_us(uint64_t now_us)
{
    return hk_app_switch_poll_interval_us(&s_integration.switcher, now_us);
}

const hk_app_t *app_runtime_integration_active(void)
{
    return hk_app_switch_active(&s_integration.switcher);
}
