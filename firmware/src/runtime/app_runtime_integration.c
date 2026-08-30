#include "app_runtime_integration.h"

#include <string.h>

#include <hackylens/capability/external_link.h>
#include <hackylens/capability/lights.h>

#include "../app_runtime/surface_private.h"
#include "../capabilities/capability_inventory_binding.h"
#include "../core/hk_capability_client.h"
#include "capability_owner_runtime.h"

typedef struct
{
    hk_app_switch_t switcher;
    hk_owner_t runtime_owner;
    hk_time_t time;
    hk_display_t display;
    hk_display_info_t display_info;
    uint8_t initialized;
    uint8_t display_batch_active;
} app_runtime_integration_t;

static app_runtime_integration_t s_integration;

static hk_result_t render_abort(void *user);

static hk_result_t resolve_capability(
    void *user,
    const hk_app_t *app,
    const hk_app_capability_request_t *declaration,
    hk_capability_request_t *request)
{
    (void)user;
    return hk_generated_capability_request_for(
        app->id, declaration->id, declaration->instance, request);
}

static hk_result_t resolve_service(
    void *user,
    const hk_app_t *app,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)app;
    (void)declaration;
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t owner_open(
    void *user,
    const hk_app_t *app,
    hk_owner_t *owner)
{
    hk_result_t result;

    (void)user;
    if(!owner)
        return HK_ERR_INVALID_ARGUMENT;
    *owner = HK_OWNER_NONE;
    result = capability_owner_runtime_enter(app);
    if(result == HK_OK)
        *owner = capability_owner_runtime_current(app);
    return result;
}

static uint32_t lights_channels(uint64_t features)
{
    uint32_t channels = 0U;

    if(features & HK_LIGHTS_FEATURE_BACKLIGHT)
        channels |= HK_LIGHTS_CHANNEL_BACKLIGHT;
    if(features & HK_LIGHTS_FEATURE_ILLUMINATION)
        channels |= HK_LIGHTS_CHANNEL_ILLUMINATION;
    if(features & HK_LIGHTS_FEATURE_RGB)
        channels |= HK_LIGHTS_CHANNEL_RGB;
    return channels;
}

static hk_result_t acquire_capability(
    void *user,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_lease_t *lease)
{
    hk_result_t result;

    (void)user;
    if(!request || !lease)
        return HK_ERR_INVALID_ARGUMENT;
    *lease = HK_LEASE_NONE;
    if(request->id == HK_CAPABILITY_ID_TIME)
    {
        hk_time_t handle = {0};
        result = hk_time_acquire(owner, request, &handle);
        *lease = handle.lease;
        return result;
    }
    if(request->id == HK_CAPABILITY_ID_INPUT)
    {
        hk_input_t handle = {0};
        result = hk_input_acquire(owner, request, &handle);
        *lease = handle.lease;
        return result;
    }
    if(request->id == HK_CAPABILITY_ID_DISPLAY)
    {
        hk_display_t handle = {0};
        result = hk_display_acquire(
            owner, request, HK_DISPLAY_PLANE_BASE, &handle);
        *lease = handle.lease;
        return result;
    }
    if(request->id == HK_CAPABILITY_ID_LIGHTS)
    {
        hk_lights_t handle = {0};
        uint32_t channels = lights_channels(request->required_features);

        if(channels == 0U)
            return HK_ERR_INVALID_ARGUMENT;
        result = hk_lights_acquire(owner, request, channels, &handle);
        *lease = handle.lease;
        return result;
    }
    if(request->id == HK_CAPABILITY_ID_EXTERNAL_LINK)
    {
        hk_external_link_t handle = {0};
        uint64_t modes = request->required_features &
                         HK_EXTERNAL_LINK_FEATURES_0_1;

        if(modes == 0U)
            return HK_ERR_INVALID_ARGUMENT;
        result = hk_external_link_acquire(owner, request, modes, &handle);
        *lease = handle.lease;
        return result;
    }
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t acquire_service(
    void *user,
    hk_owner_t owner,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)owner;
    (void)declaration;
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t owner_cleanup(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    (void)user;
    return capability_owner_runtime_close(owner, deadline);
}

static hk_result_t now_us(void *user, uint64_t *value)
{
    app_runtime_integration_t *integration = user;

    return hk_time_now_us(
        integration->runtime_owner, &integration->time, value);
}

static hk_result_t deadline_after_us(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    app_runtime_integration_t *integration = user;

    return hk_time_deadline_after_us(
        integration->runtime_owner, &integration->time,
        duration_us, deadline);
}

static hk_result_t legacy_open(void *user, const hk_app_t *app)
{
    (void)user;
    return capability_owner_runtime_enter(app);
}

static hk_result_t legacy_close(void *user, const hk_app_t *app)
{
    (void)user;
    return capability_owner_runtime_exit(app);
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
        integration->switcher.runtime.owner,
        &integration->display, region);
}

static hk_result_t surface_clear(void *user, uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;
    return hk_display_clear(
        integration->switcher.runtime.owner,
        &integration->display, rgb565);
}

static hk_result_t surface_fill_rect(
    void *user,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;
    return hk_display_fill_rect(
        integration->switcher.runtime.owner,
        &integration->display, rect, rgb565);
}

static hk_result_t surface_stroke_rect(
    void *user,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;
    return hk_display_stroke_rect(
        integration->switcher.runtime.owner,
        &integration->display, rect, rgb565);
}

static hk_result_t surface_text(
    void *user,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    app_runtime_integration_t *integration = user;
    return hk_display_text(
        integration->switcher.runtime.owner,
        &integration->display, bounds, utf8, size_bytes, rgb565);
}

static hk_result_t surface_blit(
    void *user,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    app_runtime_integration_t *integration = user;
    return hk_display_blit(
        integration->switcher.runtime.owner,
        &integration->display, destination, pixels, pixel_format);
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
    };
    app_runtime_integration_t *integration = user;
    const hk_app_context_t *ctx = &runtime->context;
    hk_result_t result = HK_ERR_CAPABILITY_ABSENT;

    integration->display = (hk_display_t){0};
    for(uint16_t index = 0U; index < ctx->capability_count; index++)
    {
        const hk_app_capability_grant_t *grant = &ctx->capabilities[index];

        if(grant->id != HK_CAPABILITY_ID_DISPLAY || grant->instance != 0U)
            continue;
        if(!grant->available)
            return HK_ERR_CAPABILITY_ABSENT;
        integration->display.lease = grant->lease;
        result = HK_OK;
        break;
    }
    if(result != HK_OK)
        return result;
    result = hk_display_get_info(
        runtime->owner, &integration->display,
        &integration->display_info);
    if(result == HK_OK)
        result = hk_display_begin_batch(runtime->owner, &integration->display);
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
        integration->switcher.runtime.owner,
        &integration->display, deadline, NULL);

    if(result == HK_OK)
        integration->display_batch_active = 0U;
    return result;
}

static hk_result_t render_abort(void *user)
{
    app_runtime_integration_t *integration = user;
    hk_result_t result;

    if(!integration->display_batch_active)
        return HK_OK;
    result = hk_display_abort(
        integration->switcher.runtime.owner,
        &integration->display);
    if(result == HK_OK)
        integration->display_batch_active = 0U;
    return result;
}

hk_result_t app_runtime_integration_initialize(void)
{
    static const hk_app_switch_ops_t switch_ops = {
        .legacy_open = legacy_open,
        .legacy_close = legacy_close,
        .now_us = now_us,
        .render_begin = render_begin,
        .render_present = render_present,
        .render_abort = render_abort,
    };
    hk_app_runtime_ops_t runtime_ops;
    hk_result_t result;
    static const hk_capability_request_t time_request =
        HK_TIME_REQUEST_0_1_INIT;

    if(s_integration.initialized)
        return HK_OK;
    memset(&s_integration, 0, sizeof(s_integration));
    result = capability_owner_runtime_initialize();
    if(result != HK_OK)
        return result;
    s_integration.runtime_owner = capability_client_consumer_owner(
        "consumer:firmware-runtime");
    if(hk_owner_is_zero(s_integration.runtime_owner))
        return HK_ERR_STALE_HANDLE;
    result = hk_time_acquire(
        s_integration.runtime_owner, &time_request, &s_integration.time);
    if(result != HK_OK)
        return result;
    runtime_ops = (hk_app_runtime_ops_t){
        .user = &s_integration,
        .resolve_capability = resolve_capability,
        .resolve_service = resolve_service,
        .owner_open = owner_open,
        .acquire_capability = acquire_capability,
        .acquire_service = acquire_service,
        .owner_cleanup = owner_cleanup,
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
