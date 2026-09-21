#include <hackylens/capability/external_link.h>
#include "app_runtime_host_support.h"

#include <string.h>

#include <hackylens/capability/display.h>
#include <hackylens/capability/input.h>
#include <hackylens/capability/time.h>

#include "capability_fake_display.h"
#include "capability_fake_external_link.h"
#include "input_normative_backend.h"
#include "time_normative_backend.h"
#include "lights_normative_backend.h"

static hk_result_t prepare(void *user, const hk_app_t *app)
{
    hk_app_runtime_host_t *host = user;
    (void)app;
    host->prepare_calls++;
    return host->fail_prepare_result;
}

static hk_result_t cleanup(void *user, hk_deadline_t deadline)
{
    hk_app_runtime_host_t *host = user;
    host->cleanup_calls++;
    host->cleanup_deadline = deadline;
    return host->fail_cleanup_result;
}

static hk_result_t deadline_after_us(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    uint64_t now_us = time_normative_backend_now_us();

    (void)user;
    if(duration_us == 0U || now_us > UINT64_MAX - duration_us)
        return HK_ERR_LIMIT;
    deadline->at_us = now_us + duration_us;
    return HK_OK;
}

static hk_result_t host_now_us(void *user, uint64_t *now_us)
{
    (void)user;
    if(!now_us)
        return HK_ERR_INVALID_ARGUMENT;
    *now_us = time_normative_backend_now_us();
    return HK_OK;
}

static hk_result_t surface_invalidate(
    void *user, const hk_display_rect_t *region)
{
    (void)user;
    (void)region;
    return HK_OK;
}

static hk_result_t surface_clear(void *user, uint16_t rgb565)
{
    (void)user;
    (void)rgb565;
    return HK_OK;
}

static hk_result_t surface_rect(
    void *user, const hk_display_rect_t *rect, uint16_t rgb565)
{
    (void)user;
    (void)rect;
    (void)rgb565;
    return HK_OK;
}

static hk_result_t surface_text(
    void *user,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    (void)user;
    (void)bounds;
    (void)utf8;
    (void)size_bytes;
    (void)rgb565;
    return HK_OK;
}

static hk_result_t surface_blit(
    void *user,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    (void)user;
    (void)destination;
    (void)pixels;
    (void)pixel_format;
    return HK_OK;
}

static hk_result_t surface_lock(void *user, hk_display_surface_t *pixels)
{
    static uint8_t s_pixels[
        HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U];

    (void)user;
    if(!pixels)
        return HK_ERR_INVALID_ARGUMENT;
    *pixels = (hk_display_surface_t){
        sizeof(hk_display_surface_t), HK_DISPLAY_SURFACE_VERSION,
        {
            s_pixels, sizeof(s_pixels),
            HK_FAKE_DISPLAY_WIDTH * 2U,
            HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE,
        },
        HK_FAKE_DISPLAY_WIDTH, HK_FAKE_DISPLAY_HEIGHT,
        HK_DISPLAY_FORMAT_RGB565_BE, 0U,
    };
    return HK_OK;
}

static hk_result_t render_begin(
    void *user,
    const hk_app_runtime_t *runtime,
    hk_app_surface_t *surface)
{
    static const hk_display_info_t info = {
        sizeof(hk_display_info_t), HK_DISPLAY_INFO_VERSION,
        HK_FAKE_DISPLAY_WIDTH, HK_FAKE_DISPLAY_HEIGHT,
        HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_BASE,
        2U, 2U, HK_FAKE_DISPLAY_MAX_COMMANDS, HK_FAKE_DISPLAY_MAX_TEXT_BYTES,
        HK_FAKE_DISPLAY_MAX_DIRTY_RECTS, HK_FAKE_DISPLAY_MAX_BORROWED_VIEWS,
        HK_FAKE_DISPLAY_TRANSFER_SLICE_BYTES, HK_FAKE_DISPLAY_MAX_PRESENT_US,
        0U,
    };
    hk_app_runtime_host_t *host = user;
    hk_app_surface_ops_t ops = {
        .user = host,
        .invalidate = surface_invalidate,
        .clear = surface_clear,
        .fill_rect = surface_rect,
        .stroke_rect = surface_rect,
        .text = surface_text,
        .blit = surface_blit,
        .lock = surface_lock,
    };

    host->batch_active = 1U;
    return hk_app_surface_private_init(
        surface, runtime->context_generation, &info, &ops);
}

static hk_result_t render_present(void *user, hk_deadline_t deadline)
{
    hk_app_runtime_host_t *host = user;

    (void)deadline;
    host->present_calls++;
    host->batch_active = 0U;
    return HK_OK;
}

static hk_result_t render_abort(void *user)
{
    hk_app_runtime_host_t *host = user;

    if(host->batch_active)
    {
        host->abort_calls++;
        host->batch_active = 0U;
    }
    return HK_OK;
}

hk_result_t hk_app_runtime_host_init(hk_app_runtime_host_t *host)
{
    hk_app_runtime_ops_t runtime_ops;
    hk_app_switch_ops_t switch_ops;
    uint64_t now_us;
    hk_result_t result;

    if(!host)
        return HK_ERR_INVALID_ARGUMENT;
    memset(host, 0, sizeof(*host));
    now_us = time_normative_backend_reset();
    input_normative_backend_reset();
    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURES_0_1);
    lights_normative_backend_reset(now_us);
    hk_fake_display_set_now_us(now_us);
    result = input_normative_backend_sample(now_us, 0U);
    if(result != HK_OK && result != HK_PENDING)
        return result;
    host->last_input_us = now_us;
    runtime_ops = (hk_app_runtime_ops_t){
        .user = host,
        .time = hk_time_service(),
        .input = hk_input_service(),
        .lights = hk_lights_service(),
        .display = hk_display_service(),
        .external_link = hk_external_link_service(),
        .prepare = prepare,
        .cleanup = cleanup,
        .deadline_after_us = deadline_after_us,
    };
    switch_ops = (hk_app_switch_ops_t){
        .user = host,
        .now_us = host_now_us,
        .render_begin = render_begin,
        .render_present = render_present,
        .render_abort = render_abort,
    };
    return hk_app_switch_init(
        &host->switcher, &runtime_ops, &switch_ops,
        HK_APP_RUNTIME_HOST_TEARDOWN_BUDGET_US);
}

hk_app_runtime_t *hk_app_runtime_host_runtime(hk_app_runtime_host_t *host)
{
    return host ? &host->switcher.runtime : NULL;
}

hk_app_switch_t *hk_app_runtime_host_switch(hk_app_runtime_host_t *host)
{
    return host ? &host->switcher : NULL;
}

uint64_t hk_app_runtime_host_now_us(const hk_app_runtime_host_t *host)
{
    (void)host;
    return time_normative_backend_now_us();
}

hk_result_t hk_app_runtime_host_set_now_us(
    hk_app_runtime_host_t *host, uint64_t now_us)
{
    if(!host)
        return HK_ERR_INVALID_ARGUMENT;
    time_normative_backend_set_now(now_us);
    hk_fake_display_set_now_us(now_us);
    return HK_OK;
}

hk_result_t hk_app_runtime_host_advance_us(
    hk_app_runtime_host_t *host, uint64_t delta_us)
{
    uint64_t now_us = time_normative_backend_now_us();

    if(!host)
        return HK_ERR_INVALID_ARGUMENT;
    if(now_us > UINT64_MAX - delta_us)
        return HK_ERR_LIMIT;
    return hk_app_runtime_host_set_now_us(host, now_us + delta_us);
}

hk_result_t hk_app_runtime_host_push_input(
    hk_app_runtime_host_t *host, uint32_t raw_state)
{
    uint64_t start_us;
    hk_result_t result;

    if(!host)
        return HK_ERR_INVALID_ARGUMENT;
    start_us = host->last_input_us + HK_INPUT_SAMPLE_INTERVAL_US;
    result = input_normative_backend_sample(start_us, raw_state);
    if(result != HK_OK)
        return result;
    result = input_normative_backend_sample(
        start_us + HK_INPUT_SAMPLE_INTERVAL_US, raw_state);
    if(result != HK_OK)
        return result;
    result = input_normative_backend_sample(
        start_us + HK_INPUT_DEBOUNCE_INTERVAL_US, raw_state);
    if(result != HK_OK)
        return result;
    host->last_input_us = start_us + HK_INPUT_DEBOUNCE_INTERVAL_US;
    return hk_app_runtime_host_set_now_us(host, host->last_input_us);
}

void hk_app_runtime_host_fail_prepare(hk_app_runtime_host_t *host, hk_result_t result)
{
    if(host) host->fail_prepare_result = result;
}

void hk_app_runtime_host_fail_cleanup(hk_app_runtime_host_t *host, hk_result_t result)
{
    if(host) host->fail_cleanup_result = result;
}

uint32_t hk_app_runtime_host_cleanup_calls(const hk_app_runtime_host_t *host)
{
    return host ? host->cleanup_calls : 0U;
}

hk_deadline_t hk_app_runtime_host_cleanup_deadline(const hk_app_runtime_host_t *host)
{
    return host ? host->cleanup_deadline : HK_DEADLINE_IMMEDIATE;
}

void hk_app_runtime_host_fill_app(
    hk_app_t *app,
    const char *id,
    const hk_app_v2_entry_t *entry,
    uint32_t state_bytes)
{
    if(!app)
        return;
    memset(app, 0, sizeof(*app));
    app->struct_size = sizeof(*app);
    app->struct_version = HK_APP_DESCRIPTOR_VERSION;
    app->id = id;
    app->entry = entry;
    app->limits.static_ram_bytes =
        entry && entry->state_capacity_bytes > state_bytes ?
            entry->state_capacity_bytes : state_bytes;
    app->limits.stack_bytes = 256U;
    app->limits.state_bytes = state_bytes;
    app->limits.state_alignment = HK_APP_STATE_ALIGNMENT;
    app->limits.tick_interval_us = 500U;
    app->limits.tick_budget_us = 100U;
    app->limits.render_budget_us = 100U;
}
