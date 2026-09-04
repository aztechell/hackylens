#include "app_runtime_host_support.h"

#include <string.h>

#include <hackylens/capability/display.h>
#include <hackylens/capability/input.h>
#include <hackylens/capability/time.h>

#include "capability_core_binding.h"
#include "capability_fake_display.h"
#include "input_normative_backend.h"
#include "time_normative_backend.h"

static hk_app_runtime_host_t *s_host;

static const char *const s_time_features[] = {
    "monotonic-us",
    "sleep-until",
};
static const char *const s_input_features[] = {
    "state",
    "events",
    "debounced-buttons",
};
static const char *const s_display_features[] = {
    "base-plane",
    "batch",
    "dirty-regions",
    "rgb565",
};
static const hk_app_capability_request_t s_capabilities[] = {
    {
        "hackylens.cap.time", 0U, "0.1.0", "0.2.0",
        s_time_features, 2U, NULL, 0U,
    },
    {
        "hackylens.cap.input", 0U, "0.1.0", "0.2.0",
        s_input_features, 3U, NULL, 0U,
    },
    {
        "hackylens.cap.display", 0U, "0.1.0", "0.2.0",
        s_display_features, 4U, NULL, 0U,
    },
};
static const hk_app_service_request_t s_services[] = {
    {"hackylens.service.fixture", "minimal-fixture.service"},
};

hk_result_t capability_owner_runtime_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_capability_id_t expected_type,
    hk_lease_t *lease)
{
    if(!s_host)
        return HK_ERR_INVALID_STATE;
    return hk_capability_core_acquire(
        &s_host->core, owner, request, expected_type, 0U, lease);
}

hk_result_t capability_owner_runtime_release(
    hk_owner_t owner,
    hk_capability_id_t expected_type,
    hk_deadline_t deadline,
    hk_lease_t *lease)
{
    if(!s_host)
        return HK_ERR_INVALID_STATE;
    return hk_capability_core_release(
        &s_host->core, owner, expected_type, 0U, deadline, lease);
}

hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    void **provider_context)
{
    if(!s_host)
        return HK_ERR_INVALID_STATE;
    return hk_capability_core_validate_lease(
        &s_host->core, owner, lease, expected_type, 0U, provider_context);
}

hk_result_t capability_owner_runtime_quarantine(
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type)
{
    if(!s_host)
        return HK_ERR_INVALID_STATE;
    return hk_capability_core_quarantine_lease(
        &s_host->core, owner, lease, expected_type, 0U);
}

static hk_result_t parse_version(const char *text, hk_version_t *version)
{
    uint32_t components[3] = {0U, 0U, 0U};

    if(!text || !version)
        return HK_ERR_INVALID_ARGUMENT;
    for(uint32_t index = 0U; index < 3U; index++)
    {
        uint32_t digits = 0U;

        while(*text >= '0' && *text <= '9')
        {
            components[index] = components[index] * 10U +
                                (uint32_t)(*text - '0');
            if(components[index] > UINT16_MAX)
                return HK_ERR_VERSION_INCOMPATIBLE;
            text++;
            digits++;
        }
        if(digits == 0U || (index < 2U && *text++ != '.') ||
           (index == 2U && *text != '\0'))
            return HK_ERR_VERSION_INCOMPATIBLE;
    }
    *version = (hk_version_t){
        (uint16_t)components[0],
        (uint16_t)components[1],
        (uint16_t)components[2],
        0U,
    };
    return HK_OK;
}

static hk_capability_id_t capability_id(const char *id)
{
    if(id && strcmp(id, "hackylens.cap.time") == 0)
        return HK_CAPABILITY_ID_TIME;
    if(id && strcmp(id, "hackylens.cap.input") == 0)
        return HK_CAPABILITY_ID_INPUT;
    if(id && strcmp(id, "hackylens.cap.display") == 0)
        return HK_CAPABILITY_ID_DISPLAY;
    return 0U;
}

static hk_result_t feature_mask(
    hk_capability_id_t id,
    const char *const *features,
    uint16_t feature_count,
    uint64_t *mask)
{
    *mask = 0U;
    for(uint16_t index = 0U; index < feature_count; index++)
    {
        const char *feature = features[index];

        if(id == HK_CAPABILITY_ID_TIME &&
           strcmp(feature, "monotonic-us") == 0)
            *mask |= HK_TIME_FEATURE_MONOTONIC_US;
        else if(id == HK_CAPABILITY_ID_TIME &&
                strcmp(feature, "sleep-until") == 0)
            *mask |= HK_TIME_FEATURE_SLEEP_UNTIL;
        else if(id == HK_CAPABILITY_ID_INPUT &&
                strcmp(feature, "state") == 0)
            *mask |= HK_INPUT_FEATURE_STATE;
        else if(id == HK_CAPABILITY_ID_INPUT &&
                strcmp(feature, "events") == 0)
            *mask |= HK_INPUT_FEATURE_EVENTS;
        else if(id == HK_CAPABILITY_ID_INPUT &&
                strcmp(feature, "debounced-buttons") == 0)
            *mask |= HK_INPUT_FEATURE_DEBOUNCED_BUTTONS;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "base-plane") == 0)
            *mask |= HK_DISPLAY_FEATURE_BASE_PLANE;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "overlay-plane") == 0)
            *mask |= HK_DISPLAY_FEATURE_OVERLAY_PLANE;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "batch") == 0)
            *mask |= HK_DISPLAY_FEATURE_BATCH;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "dirty-regions") == 0)
            *mask |= HK_DISPLAY_FEATURE_DIRTY_REGIONS;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "rgb565") == 0)
            *mask |= HK_DISPLAY_FEATURE_RGB565;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "borrowed-surface") == 0)
            *mask |= HK_DISPLAY_FEATURE_BORROWED_SURFACE;
        else if(id == HK_CAPABILITY_ID_DISPLAY &&
                strcmp(feature, "text") == 0)
            *mask |= HK_DISPLAY_FEATURE_TEXT;
        else
            return HK_ERR_FEATURE_UNAVAILABLE;
    }
    return HK_OK;
}

static hk_result_t resolve_capability(
    void *user,
    const hk_app_t *app,
    const hk_app_capability_request_t *declaration,
    hk_capability_request_t *request)
{
    hk_capability_id_t id = capability_id(declaration->id);
    hk_result_t result;

    (void)user;
    (void)app;
    memset(request, 0, sizeof(*request));
    request->struct_size = sizeof(*request);
    request->struct_version = HK_CAPABILITY_REQUEST_VERSION;
    request->id = id != 0U ? id : HK_CAPABILITY_ID_TIME;
    request->instance = declaration->instance;
    result = parse_version(declaration->minimum, &request->minimum);
    if(result != HK_OK)
        return result;
    result = parse_version(
        declaration->maximum_exclusive, &request->maximum_exclusive);
    if(result != HK_OK)
        return result;
    if(id == 0U)
        return HK_ERR_NOT_DECLARED;
    return feature_mask(
        id, declaration->features, declaration->feature_count,
        &request->required_features);
}

static hk_result_t resolve_service(
    void *user,
    const hk_app_t *app,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)app;
    if(!declaration || !declaration->id ||
       strcmp(declaration->id, "hackylens.service.fixture") != 0)
        return HK_ERR_NOT_DECLARED;
    return HK_OK;
}

static hk_result_t owner_open(
    void *user,
    const hk_app_t *app,
    hk_owner_t *owner)
{
    hk_app_runtime_host_t *host = user;

    (void)app;
    host->owner_open_calls++;
    return hk_capability_core_owner_open(
        &host->core, host->grants, 2U, owner);
}

static hk_result_t acquire_capability(
    void *user,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_lease_t *lease)
{
    hk_app_runtime_host_t *host = user;
    hk_result_t result;

    *lease = HK_LEASE_NONE;
    if(host->fail_acquire_id == request->id &&
       host->fail_acquire_result != HK_OK)
        return host->fail_acquire_result;
    if(request->id == HK_CAPABILITY_ID_TIME)
    {
        hk_time_t handle;

        result = hk_time_acquire(owner, request, &handle);
        if(result == HK_OK)
            *lease = handle.lease;
        return result;
    }
    if(request->id == HK_CAPABILITY_ID_INPUT)
    {
        hk_input_t handle;

        result = hk_input_acquire(owner, request, &handle);
        if(result == HK_OK)
            *lease = handle.lease;
        return result;
    }
    if(request->id == HK_CAPABILITY_ID_DISPLAY)
    {
        hk_display_t handle;

        result = hk_display_acquire(
            owner, request, HK_DISPLAY_PLANE_BASE, &handle);
        if(result == HK_OK)
        {
            *lease = handle.lease;
            host->display_lease = handle.lease;
            host->display_held = 1U;
        }
        return result;
    }
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t acquire_service(
    void *user,
    hk_owner_t owner,
    const hk_app_service_request_t *declaration)
{
    hk_app_runtime_host_t *host = user;

    (void)owner;
    (void)declaration;
    if(host->fail_service_result != HK_OK)
        return host->fail_service_result;
    return HK_OK;
}

static hk_result_t owner_cleanup(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    hk_app_runtime_host_t *host = user;
    hk_result_t result;

    host->owner_cleanup_calls++;
    host->owner_deadline = deadline;
    if(host->display_held)
    {
        hk_display_t handle = {.lease = host->display_lease};

        (void)hk_display_release(owner, deadline, &handle);
        host->display_held = 0U;
        host->display_lease = HK_LEASE_NONE;
    }
    result = hk_capability_core_owner_close(
        &host->core, owner, 0U, deadline);
    if(host->fail_owner_cleanup_result != HK_OK)
        return host->fail_owner_cleanup_result;
    return result;
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

static hk_result_t time_provider_cleanup(
    void *context,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    (void)context;
    (void)owner;
    (void)deadline;
    if(!s_host)
        return HK_ERR_INVALID_STATE;
    return s_host->fail_provider_cleanup_result;
}

static hk_result_t host_now_us(void *user, uint64_t *now_us)
{
    (void)user;
    if(!now_us)
        return HK_ERR_INVALID_ARGUMENT;
    *now_us = time_normative_backend_now_us();
    return HK_OK;
}

static hk_result_t legacy_open(void *user, const hk_app_t *app)
{
    (void)user;
    (void)app;
    return HK_ERR_INVALID_STATE;
}

static hk_result_t legacy_close(void *user, const hk_app_t *app)
{
    (void)user;
    (void)app;
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
    s_host = host;
    now_us = time_normative_backend_reset();
    input_normative_backend_reset();
    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    hk_fake_display_set_now_us(now_us);
    result = input_normative_backend_sample(now_us, 0U);
    if(result != HK_OK && result != HK_PENDING)
        return result;
    host->last_input_us = now_us;
    host->time_limits[0] = (hk_capability_limit_t){
        sizeof(hk_capability_limit_t), HK_CAPABILITY_LIMIT_VERSION,
        HK_TIME_LIMIT_MAX_SLEEP_US, HK_TIME_MAX_SLEEP_US,
    };
    host->inventory[0] = (hk_capability_info_t){
        sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
        HK_CAPABILITY_ID_TIME, {0U, 1U, 0U, 0U}, HK_TIME_FEATURES_0_1,
        HK_CAPABILITY_FLAG_SHARED, 0U, HK_CAPABILITY_CORE_ANY,
        host->time_limits, 1U, 0U,
    };
    host->inventory[1] = (hk_capability_info_t){
        sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
        HK_CAPABILITY_ID_INPUT, {0U, 1U, 0U, 0U}, HK_INPUT_FEATURES_0_1,
        HK_CAPABILITY_FLAG_SHARED, 0U, HK_CAPABILITY_CORE_ANY,
        NULL, 0U, 0U,
    };
    host->time_provider = *time_normative_backend_provider();
    host->time_provider.cleanup = time_provider_cleanup;
    host->providers[0] = &host->time_provider;
    host->providers[1] = input_normative_backend_provider();
    host->grants[0].request = (hk_capability_request_t)HK_TIME_REQUEST_0_1_INIT;
    host->grants[1].request = (hk_capability_request_t)HK_INPUT_REQUEST_0_1_INIT;
    result = hk_capability_core_init(
        &host->core, host->inventory, host->providers, 2U);
    if(result != HK_OK)
        return result;
    runtime_ops = (hk_app_runtime_ops_t){
        .user = host,
        .resolve_capability = resolve_capability,
        .resolve_service = resolve_service,
        .owner_open = owner_open,
        .acquire_capability = acquire_capability,
        .acquire_service = acquire_service,
        .owner_cleanup = owner_cleanup,
        .deadline_after_us = deadline_after_us,
    };
    switch_ops = (hk_app_switch_ops_t){
        .user = host,
        .legacy_open = legacy_open,
        .legacy_close = legacy_close,
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

void hk_app_runtime_host_fail_acquire(
    hk_app_runtime_host_t *host,
    hk_capability_id_t id,
    hk_result_t result)
{
    if(!host)
        return;
    host->fail_acquire_id = id;
    host->fail_acquire_result = result;
}

void hk_app_runtime_host_fail_service(
    hk_app_runtime_host_t *host, hk_result_t result)
{
    if(host)
        host->fail_service_result = result;
}

void hk_app_runtime_host_fail_owner_cleanup(
    hk_app_runtime_host_t *host, hk_result_t result)
{
    if(host)
        host->fail_owner_cleanup_result = result;
}

void hk_app_runtime_host_fail_provider_cleanup(
    hk_app_runtime_host_t *host, hk_result_t result)
{
    if(host)
        host->fail_provider_cleanup_result = result;
}

uint32_t hk_app_runtime_host_owner_cleanup_calls(
    const hk_app_runtime_host_t *host)
{
    return host ? host->owner_cleanup_calls : 0U;
}

hk_deadline_t hk_app_runtime_host_owner_deadline(
    const hk_app_runtime_host_t *host)
{
    return host ? host->owner_deadline : (hk_deadline_t){0U};
}

uint8_t hk_app_runtime_host_time_quarantined(
    const hk_app_runtime_host_t *host)
{
    return (uint8_t)(host && host->core.provider_state[0].quarantined);
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
    app->lifecycle = HK_APP_LIFECYCLE_V2;
    app->entry.v2 = entry;
    app->limits.static_ram_bytes =
        entry && entry->state_capacity_bytes > state_bytes ?
            entry->state_capacity_bytes : state_bytes;
    app->limits.stack_bytes = 256U;
    app->limits.state_bytes = state_bytes;
    app->limits.state_alignment = HK_APP_STATE_ALIGNMENT;
    app->limits.tick_interval_us = 500U;
    app->limits.tick_budget_us = 100U;
    app->limits.render_budget_us = 100U;
    app->capabilities = s_capabilities;
    app->capability_count =
        (uint16_t)(sizeof(s_capabilities) / sizeof(s_capabilities[0]));
    app->services = s_services;
    app->service_count =
        (uint16_t)(sizeof(s_services) / sizeof(s_services[0]));
}
