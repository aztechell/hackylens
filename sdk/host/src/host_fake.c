#include <hackylens/app/host_fake.h>

#include <limits.h>
#include <string.h>

#define HK_APP_HOST_FAKE_MAGIC UINT32_C(0x484B464B)
#define HK_APP_HOST_FAKE_LEASE_CAPACITY 24U
#define HK_APP_HOST_FAKE_DISPLAY_MAX_COMMANDS 32U
#define HK_APP_HOST_FAKE_DISPLAY_MAX_TEXT_BYTES 128U
#define HK_APP_HOST_FAKE_DISPLAY_MAX_DIRTY_RECTS HK_APP_MAX_INVALIDATIONS
#define HK_APP_HOST_FAKE_DISPLAY_MAX_PRESENT_US UINT32_C(100000)

typedef enum
{
    HK_APP_HOST_FAKE_DISPLAY_IDLE = 0,
    HK_APP_HOST_FAKE_DISPLAY_BATCH,
    HK_APP_HOST_FAKE_DISPLAY_SURFACE,
} hk_app_host_fake_display_state_t;

typedef struct
{
    hk_lease_t lease;
    uint64_t input_next_sequence;
    hk_display_rect_t display_clip;
    uint32_t display_plane;
    uint16_t display_commands;
    uint16_t display_text_bytes;
    uint16_t display_dirty_rects;
    uint8_t active;
    uint8_t display_state;
} hk_app_host_fake_lease_t;

typedef struct hk_app_host_fake_impl hk_app_host_fake_impl_t;

struct hk_app_surface
{
    hk_app_host_fake_impl_t *fake;
    uint32_t context_generation;
    uint8_t valid;
};

struct hk_app_host_fake_impl
{
    uint32_t magic;
    hk_app_host_fake_config_t config;
    hk_app_host_fake_grant_t grants[HK_APP_CONTEXT_MAX_CAPABILITIES];
    hk_app_host_fake_service_t services[HK_APP_CONTEXT_MAX_SERVICES];
    hk_app_context_t context;
    hk_app_surface_t surface;
    hk_input_event_t input_events[HK_APP_HOST_FAKE_INPUT_CAPACITY];
    hk_app_host_fake_lease_t leases[HK_APP_HOST_FAKE_LEASE_CAPACITY];
    hk_app_host_fake_state_t state;
    hk_result_t first_error;
    hk_app_host_fake_failure_point_t failure_point;
    hk_result_t failure_result;
    hk_deadline_t teardown_deadline;
    uint64_t now_us;
    uint64_t next_tick_us;
    uint64_t event_sequence;
    uint64_t input_sequence;
    uint32_t context_generation;
    uint32_t input_state;
    uint32_t probe_calls;
    uint32_t prepare_calls;
    uint32_t start_calls;
    uint32_t event_calls;
    uint32_t tick_calls;
    uint32_t render_calls;
    uint32_t stop_calls;
    uint32_t cleanup_calls;
    uint32_t owner_cleanup_calls;
    uint32_t display_operations;
    uint32_t display_present_calls;
    uint8_t callback_active;
    uint8_t prepare_entered;
    uint8_t start_entered;
    uint8_t teardown_deadline_valid;
    uint8_t render_pending;
};

_Static_assert(
    sizeof(hk_app_host_fake_impl_t) <= HK_APP_HOST_FAKE_STORAGE_BYTES,
    "HK_APP_HOST_FAKE_STORAGE_BYTES is too small");

static hk_app_host_fake_impl_t *s_active_fake;
static hk_app_host_fake_impl_t *s_callback_fake;

static hk_app_host_fake_impl_t *fake_impl(hk_app_host_fake_t *fake)
{
    hk_app_host_fake_impl_t *impl = (hk_app_host_fake_impl_t *)fake;

    if(!fake || impl->magic != HK_APP_HOST_FAKE_MAGIC)
        return NULL;
    return impl;
}

static const hk_app_host_fake_impl_t *const_fake_impl(
    const hk_app_host_fake_t *fake)
{
    const hk_app_host_fake_impl_t *impl =
        (const hk_app_host_fake_impl_t *)fake;

    if(!fake || impl->magic != HK_APP_HOST_FAKE_MAGIC)
        return NULL;
    return impl;
}

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t result_is_failure(hk_result_t result)
{
    return (uint8_t)(result != HK_OK);
}

static hk_result_t callback_result(hk_result_t result)
{
    return result == HK_PENDING ? HK_ERR_INVALID_STATE : result;
}

static void retain_error(hk_app_host_fake_impl_t *fake, hk_result_t result)
{
    result = callback_result(result);
    if(result_is_failure(result) && fake->first_error == HK_OK)
        fake->first_error = result;
}

static hk_result_t injected_result(
    hk_app_host_fake_impl_t *fake,
    hk_app_host_fake_failure_point_t point)
{
    hk_result_t result;

    if(fake->failure_point != point)
        return HK_OK;
    result = fake->failure_result;
    fake->failure_point = HK_APP_HOST_FAKE_FAIL_NONE;
    fake->failure_result = HK_OK;
    return result;
}

static hk_result_t begin_callback(
    hk_app_host_fake_impl_t *fake,
    hk_app_host_fake_state_t state)
{
    if(fake->callback_active || s_callback_fake)
        return HK_ERR_INVALID_STATE;
    fake->state = state;
    fake->callback_active = 1U;
    s_callback_fake = fake;
    return HK_OK;
}

static hk_result_t finish_callback(
    hk_app_host_fake_impl_t *fake,
    hk_result_t result)
{
    fake->callback_active = 0U;
    if(s_callback_fake == fake)
        s_callback_fake = NULL;
    return callback_result(result);
}

static hk_result_t validate_callback_context(const hk_app_context_t *ctx)
{
    if(!ctx || !s_active_fake || !s_callback_fake ||
       s_callback_fake != s_active_fake || !s_callback_fake->callback_active ||
       ctx != &s_callback_fake->context ||
       ctx->generation != s_callback_fake->context_generation)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

static hk_app_host_fake_failure_point_t grant_failure_point(
    hk_capability_id_t id)
{
    if(id == HK_CAPABILITY_ID_TIME)
        return HK_APP_HOST_FAKE_FAIL_GRANT_TIME;
    if(id == HK_CAPABILITY_ID_INPUT)
        return HK_APP_HOST_FAKE_FAIL_GRANT_INPUT;
    if(id == HK_CAPABILITY_ID_DISPLAY)
        return HK_APP_HOST_FAKE_FAIL_GRANT_DISPLAY;
    return HK_APP_HOST_FAKE_FAIL_NONE;
}

static uint8_t supported_grant(hk_capability_id_t id)
{
    return (uint8_t)(id == HK_CAPABILITY_ID_TIME ||
                     id == HK_CAPABILITY_ID_INPUT ||
                     id == HK_CAPABILITY_ID_DISPLAY);
}

static uint64_t capability_features(
    const hk_app_host_fake_impl_t *fake,
    hk_capability_id_t id)
{
    if(id == HK_CAPABILITY_ID_TIME)
        return HK_TIME_FEATURES_0_1;
    if(id == HK_CAPABILITY_ID_INPUT)
        return HK_INPUT_FEATURES_0_1;
    if(id == HK_CAPABILITY_ID_DISPLAY)
    {
        uint64_t features =
            HK_DISPLAY_FEATURE_BASE_PLANE | HK_DISPLAY_FEATURE_BATCH |
            HK_DISPLAY_FEATURE_DIRTY_REGIONS | HK_DISPLAY_FEATURE_RGB565 |
            HK_DISPLAY_FEATURE_TEXT;

        if(fake->config.display_surface.data)
            features |= HK_DISPLAY_FEATURE_BORROWED_SURFACE;
        return features;
    }
    return 0U;
}

static int version_compare(hk_version_t left, hk_version_t right)
{
    if(left.major != right.major)
        return left.major < right.major ? -1 : 1;
    if(left.minor != right.minor)
        return left.minor < right.minor ? -1 : 1;
    if(left.patch != right.patch)
        return left.patch < right.patch ? -1 : 1;
    return 0;
}

static hk_result_t validate_capability_request(
    const hk_app_host_fake_impl_t *fake,
    const hk_capability_request_t *request,
    hk_capability_id_t id)
{
    const hk_version_t provider_version = {0U, 1U, 0U, 0U};

    if(!request || request->struct_size < sizeof(*request))
        return HK_ERR_INVALID_ARGUMENT;
    if(request->struct_version != HK_CAPABILITY_REQUEST_VERSION)
        return HK_ERR_VERSION_INCOMPATIBLE;
    if(request->id != id || request->reserved != 0U ||
       request->minimum.reserved != 0U ||
       request->maximum_exclusive.reserved != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(version_compare(request->minimum, request->maximum_exclusive) >= 0)
        return HK_ERR_INVALID_ARGUMENT;
    if(version_compare(request->minimum, provider_version) > 0 ||
       version_compare(provider_version, request->maximum_exclusive) >= 0)
        return HK_ERR_VERSION_INCOMPATIBLE;
    if((request->required_features & capability_features(fake, id)) !=
       request->required_features)
        return HK_ERR_FEATURE_UNAVAILABLE;
    return HK_OK;
}

static hk_app_host_fake_lease_t *lease_record(
    hk_app_host_fake_impl_t *fake,
    const hk_lease_t *lease)
{
    hk_app_host_fake_lease_t *record;

    if(!fake || !lease || lease->slot == 0U ||
       lease->slot > HK_APP_HOST_FAKE_LEASE_CAPACITY)
        return NULL;
    record = &fake->leases[lease->slot - 1U];
    if(!record->active || record->lease.generation != lease->generation ||
       record->lease.capability_id != lease->capability_id ||
       !owner_equal(record->lease.owner, lease->owner))
        return NULL;
    return record;
}

static hk_result_t allocate_lease(
    hk_app_host_fake_impl_t *fake,
    hk_capability_id_t id,
    uint32_t display_plane,
    hk_lease_t *lease)
{
    uint32_t index;

    if(!fake || !lease)
        return HK_ERR_INVALID_ARGUMENT;
    if(id == HK_CAPABILITY_ID_DISPLAY)
    {
        for(index = 0U; index < HK_APP_HOST_FAKE_LEASE_CAPACITY; index++)
        {
            if(fake->leases[index].active &&
               fake->leases[index].lease.capability_id == id &&
               fake->leases[index].display_plane == display_plane)
                return HK_ERR_BUSY;
        }
    }
    for(index = 0U; index < HK_APP_HOST_FAKE_LEASE_CAPACITY; index++)
    {
        hk_app_host_fake_lease_t *record = &fake->leases[index];
        uint32_t generation = record->lease.generation;

        if(record->active || generation == UINT32_MAX)
            continue;
        memset(record, 0, sizeof(*record));
        record->lease = (hk_lease_t){
            index + 1U,
            generation + 1U,
            fake->context.owner,
            id,
        };
        record->input_next_sequence = fake->input_sequence + 1U;
        record->display_plane = display_plane;
        record->display_clip = (hk_display_rect_t){
            0, 0, fake->config.display_width, fake->config.display_height,
        };
        record->active = 1U;
        *lease = record->lease;
        return HK_OK;
    }
    return HK_ERR_LIMIT;
}

static void rebuild_context(hk_app_host_fake_impl_t *fake)
{
    uint16_t index;

    memset(&fake->context, 0, sizeof(fake->context));
    fake->context.struct_size = sizeof(fake->context);
    fake->context.struct_version = HK_APP_CONTEXT_VERSION;
    fake->context.app_id = fake->config.app_id;
    fake->context.generation = fake->context_generation;
    fake->context.capability_count = fake->config.grant_count;
    fake->context.service_count = fake->config.service_count;
    for(index = 0U; index < fake->config.grant_count; index++)
    {
        fake->context.capabilities[index].id = fake->grants[index].id;
        fake->context.capabilities[index].instance = fake->grants[index].instance;
        fake->context.capabilities[index].optional = fake->grants[index].optional;
        fake->context.capabilities[index].available = fake->grants[index].available;
        fake->context.capabilities[index].fallback = fake->grants[index].fallback;
        fake->context.capabilities[index].lease = HK_LEASE_NONE;
    }
    for(index = 0U; index < fake->config.service_count; index++)
    {
        fake->context.services[index].id = fake->services[index].id;
        fake->context.services[index].namespace_name =
            fake->services[index].namespace_name;
    }
}

static void retire_instance(hk_app_host_fake_impl_t *fake)
{
    if(fake->config.entry->state_storage &&
       fake->config.state_bytes > 0U)
    {
        memset(
            fake->config.entry->state_storage,
            0,
            fake->config.state_bytes);
    }
    fake->context.owner = HK_OWNER_NONE;
    memset(fake->context.capabilities, 0, sizeof(fake->context.capabilities));
    memset(fake->context.services, 0, sizeof(fake->context.services));
    memset(fake->leases, 0, sizeof(fake->leases));
    fake->surface.valid = 0U;
    fake->render_pending = 0U;
    fake->teardown_deadline_valid = 0U;
    fake->prepare_entered = 0U;
    fake->start_entered = 0U;
    fake->state = HK_APP_HOST_FAKE_INACTIVE;
    if(fake->context_generation != UINT32_MAX)
        fake->context_generation++;
}

static hk_result_t owner_cleanup(hk_app_host_fake_impl_t *fake)
{
    hk_result_t result;

    fake->owner_cleanup_calls++;
    result = injected_result(fake, HK_APP_HOST_FAKE_FAIL_OWNER_CLEANUP);
    retain_error(fake, result);
    memset(fake->leases, 0, sizeof(fake->leases));
    return result;
}

static hk_result_t teardown(
    hk_app_host_fake_impl_t *fake,
    hk_app_stop_reason_t reason)
{
    hk_result_t result;

    if(fake->config.teardown_budget_us == 0U ||
       fake->now_us > UINT64_MAX - fake->config.teardown_budget_us)
    {
        fake->teardown_deadline = HK_DEADLINE_IMMEDIATE;
        retain_error(fake, HK_ERR_OVERFLOW);
    }
    else
    {
        fake->teardown_deadline.at_us =
            fake->now_us + fake->config.teardown_budget_us;
    }
    fake->teardown_deadline_valid = 1U;

    if(fake->start_entered)
    {
        result = begin_callback(fake, HK_APP_HOST_FAKE_STOPPING);
        if(result == HK_OK)
        {
            result = injected_result(fake, HK_APP_HOST_FAKE_FAIL_STOP);
            if(result == HK_OK)
            {
                fake->stop_calls++;
                result = fake->config.entry->stop(&fake->context, reason);
            }
            result = finish_callback(fake, result);
        }
        retain_error(fake, result);
    }

    if(fake->prepare_entered)
    {
        result = begin_callback(fake, HK_APP_HOST_FAKE_CLEANING);
        if(result == HK_OK)
        {
            result = injected_result(fake, HK_APP_HOST_FAKE_FAIL_CLEANUP);
            if(result == HK_OK)
            {
                fake->cleanup_calls++;
                result = fake->config.entry->cleanup(&fake->context);
            }
            result = finish_callback(fake, result);
        }
        retain_error(fake, result);
    }

    (void)owner_cleanup(fake);
    result = fake->first_error;
    retire_instance(fake);
    return result;
}

static hk_result_t dispatch_close(
    hk_app_host_fake_impl_t *fake,
    hk_app_stop_reason_t reason)
{
    hk_app_event_t event = {
        sizeof(hk_app_event_t), HK_APP_EVENT_VERSION,
        HK_APP_EVENT_RUNTIME_CLOSE, 0U, ++fake->event_sequence,
        fake->now_us, {.close = {reason, 0U}},
    };
    hk_result_t result = begin_callback(fake, HK_APP_HOST_FAKE_RUNNING);

    if(result == HK_OK)
    {
        result = injected_result(fake, HK_APP_HOST_FAKE_FAIL_EVENT);
        if(result == HK_OK)
        {
            fake->event_calls++;
            result = fake->config.entry->event(&fake->context, &event);
        }
        result = finish_callback(fake, result);
    }
    retain_error(fake, result);
    return result;
}

static hk_result_t terminate_running(
    hk_app_host_fake_impl_t *fake,
    hk_app_stop_reason_t reason)
{
    (void)dispatch_close(fake, reason);
    return teardown(fake, reason);
}

static hk_result_t dispatch_event_value(
    hk_app_host_fake_impl_t *fake,
    hk_app_event_t event)
{
    hk_result_t result;

    if(fake->state != HK_APP_HOST_FAKE_RUNNING)
        return HK_ERR_INVALID_STATE;
    event.struct_size = sizeof(event);
    event.struct_version = HK_APP_EVENT_VERSION;
    event.sequence = ++fake->event_sequence;
    event.timestamp_us = fake->now_us;
    result = begin_callback(fake, HK_APP_HOST_FAKE_RUNNING);
    if(result == HK_OK)
    {
        result = injected_result(fake, HK_APP_HOST_FAKE_FAIL_EVENT);
        if(result == HK_OK)
        {
            fake->event_calls++;
            result = fake->config.entry->event(&fake->context, &event);
        }
        result = finish_callback(fake, result);
    }
    if(result != HK_OK)
    {
        retain_error(fake, result);
        (void)terminate_running(fake, HK_APP_STOP_CALLBACK_FAILED);
    }
    return result;
}

static hk_result_t validate_handle(
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t id,
    uint16_t *grant_index)
{
    hk_app_host_fake_lease_t *record;

    if(!s_active_fake || !lease)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_owner_is_zero(owner))
        return HK_ERR_WRONG_OWNER;
    if(hk_owner_is_zero(s_active_fake->context.owner))
        return HK_ERR_STALE_HANDLE;
    if(!owner_equal(owner, s_active_fake->context.owner))
        return HK_ERR_WRONG_OWNER;
    if(lease->capability_id != id)
        return HK_ERR_INVALID_ARGUMENT;
    record = lease_record(s_active_fake, lease);
    if(!record)
        return HK_ERR_STALE_HANDLE;
    if(grant_index)
        *grant_index = (uint16_t)(record - s_active_fake->leases);
    return HK_OK;
}

hk_result_t hk_app_host_fake_initialize(
    hk_app_host_fake_t *fake,
    const hk_app_host_fake_config_t *config)
{
    hk_app_host_fake_impl_t *impl;
    uint16_t left;
    uint16_t right;

    if(!fake || !config || config->struct_size < sizeof(*config) ||
       config->struct_version != HK_APP_HOST_FAKE_VERSION || !config->app_id ||
       !config->entry || !config->entry->state_storage ||
       config->entry->state_capacity_bytes == 0U || !config->entry->probe ||
       !config->entry->prepare || !config->entry->start ||
       !config->entry->event || !config->entry->tick ||
       !config->entry->render || !config->entry->stop ||
       !config->entry->cleanup || config->grant_count >
           HK_APP_CONTEXT_MAX_CAPABILITIES ||
       (config->grant_count > 0U && !config->grants) ||
       config->service_count > HK_APP_CONTEXT_MAX_SERVICES ||
       (config->service_count > 0U && !config->services) ||
       config->reserved != 0U || config->state_bytes == 0U ||
       config->state_bytes > config->entry->state_capacity_bytes ||
       config->tick_interval_us == 0U || config->tick_budget_us == 0U ||
       config->tick_budget_us > config->tick_interval_us ||
       config->render_budget_us == 0U || config->teardown_budget_us == 0U ||
       config->display_width == 0U || config->display_height == 0U ||
       ((uintptr_t)config->entry->state_storage % HK_APP_STATE_ALIGNMENT) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(config->display_surface.data)
    {
        uint64_t minimum_stride = (uint64_t)config->display_width * 2U;
        uint64_t minimum_size =
            (uint64_t)config->display_surface.stride_bytes *
            config->display_height;

        if(minimum_stride > UINT32_MAX ||
           config->display_surface.stride_bytes < minimum_stride ||
           minimum_size > config->display_surface.size_bytes ||
           (((uintptr_t)config->display_surface.data) & 1U) != 0U ||
           (config->display_surface.flags &
            (HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE)) !=
               (HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE))
            return HK_ERR_INVALID_ARGUMENT;
    }
    else if(config->display_surface.size_bytes != 0U ||
            config->display_surface.stride_bytes != 0U ||
            config->display_surface.flags != 0U)
    {
        return HK_ERR_INVALID_ARGUMENT;
    }
    for(left = 0U; left < config->grant_count; left++)
    {
        const hk_app_host_fake_grant_t *grant = &config->grants[left];
        if(!supported_grant(grant->id) || grant->instance != 0U ||
           grant->optional > 1U || grant->available > 1U ||
           (grant->optional && !grant->available && !grant->fallback) ||
           (!grant->optional && grant->fallback))
            return HK_ERR_INVALID_ARGUMENT;
        for(right = 0U; right < left; right++)
        {
            if(config->grants[right].id == grant->id &&
               config->grants[right].instance == grant->instance)
                return HK_ERR_INVALID_ARGUMENT;
        }
    }
    for(left = 0U; left < config->service_count; left++)
    {
        if(!config->services[left].id ||
           !config->services[left].namespace_name)
            return HK_ERR_INVALID_ARGUMENT;
        for(right = 0U; right < left; right++)
        {
            if(strcmp(config->services[right].id, config->services[left].id) == 0)
                return HK_ERR_INVALID_ARGUMENT;
        }
    }

    memset(fake, 0, sizeof(*fake));
    impl = (hk_app_host_fake_impl_t *)fake;
    impl->magic = HK_APP_HOST_FAKE_MAGIC;
    impl->config = *config;
    memcpy(
        impl->grants,
        config->grants,
        (size_t)config->grant_count * sizeof(config->grants[0]));
    impl->config.grants = impl->grants;
    if(config->service_count > 0U)
    {
        memcpy(
            impl->services,
            config->services,
            (size_t)config->service_count * sizeof(config->services[0]));
    }
    impl->config.services = impl->services;
    impl->state = HK_APP_HOST_FAKE_INACTIVE;
    impl->first_error = HK_OK;
    impl->now_us = config->initial_time_us;
    impl->context_generation = 1U;
    rebuild_context(impl);
    memset(
        impl->config.entry->state_storage,
        0,
        impl->config.state_bytes);
    s_active_fake = impl;
    return HK_OK;
}

hk_result_t hk_app_host_fake_set_failure(
    hk_app_host_fake_t *fake,
    hk_app_host_fake_failure_point_t point,
    hk_result_t result)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);

    if(!impl || point <= HK_APP_HOST_FAKE_FAIL_NONE ||
       point > HK_APP_HOST_FAKE_FAIL_OWNER_CLEANUP || result >= HK_OK)
        return HK_ERR_INVALID_ARGUMENT;
    impl->failure_point = point;
    impl->failure_result = result;
    return HK_OK;
}

hk_result_t hk_app_host_fake_advance_time(
    hk_app_host_fake_t *fake,
    uint64_t delta_us)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);

    if(!impl)
        return HK_ERR_INVALID_ARGUMENT;
    if(impl->now_us > UINT64_MAX - delta_us)
        return HK_ERR_OVERFLOW;
    impl->now_us += delta_us;
    return HK_OK;
}

hk_result_t hk_app_host_fake_launch(hk_app_host_fake_t *fake)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);
    hk_result_t result;
    uint16_t index;

    if(!impl || impl != s_active_fake)
        return HK_ERR_INVALID_ARGUMENT;
    if(impl->state != HK_APP_HOST_FAKE_INACTIVE ||
       impl->context_generation == UINT32_MAX)
        return HK_ERR_INVALID_STATE;
    impl->first_error = HK_OK;
    impl->event_sequence = 0U;
    impl->input_sequence = 0U;
    impl->input_state = 0U;
    impl->next_tick_us = 0U;
    memset(impl->input_events, 0, sizeof(impl->input_events));
    memset(impl->leases, 0, sizeof(impl->leases));
    rebuild_context(impl);
    for(index = 0U; index < impl->config.grant_count; index++)
    {
        if(!impl->grants[index].optional && !impl->grants[index].available)
        {
            impl->first_error = HK_ERR_CAPABILITY_ABSENT;
            retire_instance(impl);
            return HK_ERR_CAPABILITY_ABSENT;
        }
    }

    result = begin_callback(impl, HK_APP_HOST_FAKE_PROBING);
    if(result == HK_OK)
    {
        result = injected_result(impl, HK_APP_HOST_FAKE_FAIL_PROBE);
        if(result == HK_OK)
        {
            impl->probe_calls++;
            result = impl->config.entry->probe(&impl->context);
        }
        result = finish_callback(impl, result);
    }
    if(result != HK_OK)
    {
        retain_error(impl, result);
        retire_instance(impl);
        return result;
    }

    impl->context.owner = (hk_owner_t){1U, impl->context_generation};
    for(index = 0U; index < impl->config.grant_count; index++)
    {
        hk_app_capability_grant_t *grant = &impl->context.capabilities[index];
        if(!grant->available)
            continue;
        result = injected_result(impl, grant_failure_point(grant->id));
        if(result != HK_OK)
        {
            retain_error(impl, result);
            (void)teardown(impl, HK_APP_STOP_START_FAILED);
            return result;
        }
        result = allocate_lease(
            impl, grant->id,
            grant->id == HK_CAPABILITY_ID_DISPLAY ?
                HK_DISPLAY_PLANE_BASE : 0U,
            &grant->lease);
        if(result != HK_OK)
        {
            retain_error(impl, result);
            (void)teardown(impl, HK_APP_STOP_START_FAILED);
            return result;
        }
    }
    result = injected_result(impl, HK_APP_HOST_FAKE_FAIL_GRANT_SERVICE);
    if(result != HK_OK)
    {
        retain_error(impl, result);
        (void)teardown(impl, HK_APP_STOP_START_FAILED);
        return result;
    }
    for(index = 0U; index < impl->config.service_count; index++)
    {
        impl->context.services[index].owner = impl->context.owner;
        impl->context.services[index].context_generation =
            impl->context_generation;
    }

    impl->prepare_entered = 1U;
    result = begin_callback(impl, HK_APP_HOST_FAKE_PREPARING);
    if(result == HK_OK)
    {
        result = injected_result(impl, HK_APP_HOST_FAKE_FAIL_PREPARE);
        if(result == HK_OK)
        {
            impl->prepare_calls++;
            result = impl->config.entry->prepare(&impl->context);
        }
        result = finish_callback(impl, result);
    }
    if(result != HK_OK)
    {
        retain_error(impl, result);
        (void)teardown(impl, HK_APP_STOP_START_FAILED);
        return result;
    }

    impl->start_entered = 1U;
    result = begin_callback(impl, HK_APP_HOST_FAKE_STARTING);
    if(result == HK_OK)
    {
        result = injected_result(impl, HK_APP_HOST_FAKE_FAIL_START);
        if(result == HK_OK)
        {
            impl->start_calls++;
            result = impl->config.entry->start(&impl->context);
        }
        result = finish_callback(impl, result);
    }
    if(result != HK_OK)
    {
        retain_error(impl, result);
        (void)teardown(impl, HK_APP_STOP_START_FAILED);
        return result;
    }
    impl->state = HK_APP_HOST_FAKE_RUNNING;
    impl->render_pending = 1U;
    if(impl->now_us > UINT64_MAX - impl->config.tick_interval_us)
    {
        retain_error(impl, HK_ERR_LIMIT);
        return terminate_running(impl, HK_APP_STOP_DEADLINE);
    }
    impl->next_tick_us = impl->now_us + impl->config.tick_interval_us;
    return HK_OK;
}

hk_result_t hk_app_host_fake_input(
    hk_app_host_fake_t *fake,
    uint32_t state)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);
    hk_input_event_t input;
    hk_app_event_t event = {0};
    uint32_t changed;

    if(!impl || (state & ~HK_INPUT_BUTTON_ALL) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(impl->state != HK_APP_HOST_FAKE_RUNNING)
        return HK_ERR_INVALID_STATE;
    changed = impl->input_state ^ state;
    if(changed == 0U)
        return HK_OK;
    if(impl->input_sequence == UINT64_MAX)
        return HK_ERR_LIMIT;
    input = (hk_input_event_t){
        ++impl->input_sequence,
        impl->now_us,
        state,
        changed,
        changed & state,
        changed & ~state,
        0U,
    };
    impl->input_state = state;
    impl->input_events[
        (impl->input_sequence - 1U) % HK_APP_HOST_FAKE_INPUT_CAPACITY] = input;
    event.kind = HK_APP_EVENT_INPUT;
    event.data.input = input;
    return dispatch_event_value(impl, event);
}

hk_result_t hk_app_host_fake_media(
    hk_app_host_fake_t *fake,
    hk_app_media_kind_t kind,
    uint32_t generation)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);
    hk_app_event_t event = {0};

    if(!impl || kind < HK_APP_MEDIA_INSERTED || kind > HK_APP_MEDIA_ERROR)
        return HK_ERR_INVALID_ARGUMENT;
    event.kind = HK_APP_EVENT_MEDIA;
    event.data.media.kind = kind;
    event.data.media.generation = generation;
    return dispatch_event_value(impl, event);
}

hk_result_t hk_app_host_fake_event(
    hk_app_host_fake_t *fake,
    const hk_app_event_t *event)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);

    if(!impl || !event || event->struct_size < sizeof(*event) ||
       event->struct_version != HK_APP_EVENT_VERSION ||
       event->kind < HK_APP_EVENT_INPUT || event->kind > HK_APP_EVENT_WAKEUP ||
       event->kind == HK_APP_EVENT_RUNTIME_CLOSE)
        return HK_ERR_INVALID_ARGUMENT;
    return dispatch_event_value(impl, *event);
}

hk_result_t hk_app_host_fake_tick(hk_app_host_fake_t *fake)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);
    hk_app_event_t event = {0};
    hk_result_t result;
    uint64_t started_us;

    if(!impl)
        return HK_ERR_INVALID_ARGUMENT;
    if(impl->state != HK_APP_HOST_FAKE_RUNNING)
        return HK_ERR_INVALID_STATE;
    if(impl->now_us < impl->next_tick_us)
        return HK_PENDING;
    started_us = impl->now_us;
    event.kind = HK_APP_EVENT_TIMER;
    event.data.timer.scheduled_us = impl->next_tick_us;
    event.data.timer.now_us = impl->now_us;
    result = dispatch_event_value(impl, event);
    if(result != HK_OK || impl->state != HK_APP_HOST_FAKE_RUNNING)
        return result;
    result = begin_callback(impl, HK_APP_HOST_FAKE_RUNNING);
    if(result == HK_OK)
    {
        result = injected_result(impl, HK_APP_HOST_FAKE_FAIL_TICK);
        if(result == HK_OK)
        {
            impl->tick_calls++;
            result = impl->config.entry->tick(&impl->context, impl->now_us);
        }
        result = finish_callback(impl, result);
    }
    if(result != HK_OK)
    {
        retain_error(impl, result);
        (void)terminate_running(impl, HK_APP_STOP_CALLBACK_FAILED);
        return result;
    }
    if(impl->now_us - started_us > impl->config.tick_budget_us)
    {
        retain_error(impl, HK_ERR_DEADLINE_EXCEEDED);
        (void)terminate_running(impl, HK_APP_STOP_DEADLINE);
        return HK_ERR_DEADLINE_EXCEEDED;
    }
    if(impl->now_us > UINT64_MAX - impl->config.tick_interval_us)
    {
        retain_error(impl, HK_ERR_LIMIT);
        (void)terminate_running(impl, HK_APP_STOP_DEADLINE);
        return HK_ERR_LIMIT;
    }
    impl->next_tick_us = impl->now_us + impl->config.tick_interval_us;
    return HK_OK;
}

hk_result_t hk_app_host_fake_render(hk_app_host_fake_t *fake)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);
    hk_result_t result;
    uint64_t started_us;

    if(!impl)
        return HK_ERR_INVALID_ARGUMENT;
    if(impl->state != HK_APP_HOST_FAKE_RUNNING)
        return HK_ERR_INVALID_STATE;
    if(!impl->render_pending)
        return HK_PENDING;
    impl->render_pending = 0U;
    started_us = impl->now_us;
    impl->surface.fake = impl;
    impl->surface.context_generation = impl->context_generation;
    impl->surface.valid = 1U;
    result = begin_callback(impl, HK_APP_HOST_FAKE_RUNNING);
    if(result == HK_OK)
    {
        result = injected_result(impl, HK_APP_HOST_FAKE_FAIL_RENDER);
        if(result == HK_OK)
        {
            impl->render_calls++;
            result = impl->config.entry->render(
                &impl->context, &impl->surface);
        }
        result = finish_callback(impl, result);
    }
    impl->surface.valid = 0U;
    if(result != HK_OK)
    {
        retain_error(impl, result);
        (void)terminate_running(impl, HK_APP_STOP_CALLBACK_FAILED);
        return result;
    }
    if(impl->now_us - started_us > impl->config.render_budget_us)
    {
        retain_error(impl, HK_ERR_DEADLINE_EXCEEDED);
        (void)terminate_running(impl, HK_APP_STOP_DEADLINE);
        return HK_ERR_DEADLINE_EXCEEDED;
    }
    impl->display_present_calls++;
    return HK_OK;
}

hk_result_t hk_app_host_fake_stop(
    hk_app_host_fake_t *fake,
    hk_app_stop_reason_t reason)
{
    hk_app_host_fake_impl_t *impl = fake_impl(fake);

    if(!impl)
        return HK_ERR_INVALID_ARGUMENT;
    if(impl->state != HK_APP_HOST_FAKE_RUNNING)
        return impl->state == HK_APP_HOST_FAKE_INACTIVE
                   ? HK_OK
                   : HK_ERR_INVALID_STATE;
    if((unsigned)reason > (unsigned)HK_APP_STOP_SHUTDOWN)
        reason = HK_APP_STOP_FORCED;
    return terminate_running(impl, reason);
}

hk_result_t hk_app_host_fake_snapshot(
    const hk_app_host_fake_t *fake,
    hk_app_host_fake_snapshot_t *snapshot)
{
    const hk_app_host_fake_impl_t *impl = const_fake_impl(fake);

    if(!impl || !snapshot || snapshot->struct_size < sizeof(*snapshot) ||
       snapshot->struct_version != HK_APP_HOST_FAKE_VERSION)
        return HK_ERR_INVALID_ARGUMENT;
    *snapshot = (hk_app_host_fake_snapshot_t){
        sizeof(*snapshot), HK_APP_HOST_FAKE_VERSION,
        impl->state, impl->first_error, impl->now_us, impl->event_sequence,
        impl->context_generation, impl->probe_calls, impl->prepare_calls,
        impl->start_calls, impl->event_calls, impl->tick_calls,
        impl->render_calls, impl->stop_calls, impl->cleanup_calls,
        impl->owner_cleanup_calls, impl->display_operations,
        impl->display_present_calls, impl->render_pending, {0U, 0U, 0U},
    };
    return HK_OK;
}

hk_result_t hk_app_context_identity(
    const hk_app_context_t *ctx,
    const char **app_id,
    uint32_t *generation,
    hk_owner_t *owner)
{
    hk_result_t result;

    if(!app_id || !generation || !owner)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    *app_id = ctx->app_id;
    *generation = ctx->generation;
    *owner = ctx->owner;
    return HK_OK;
}

hk_result_t hk_app_context_capability_status(
    const hk_app_context_t *ctx,
    hk_capability_id_t id,
    uint16_t instance,
    uint8_t *available,
    const char **fallback)
{
    uint16_t index;
    hk_result_t result;

    if(id == 0U || !available || !fallback)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < ctx->capability_count; index++)
    {
        const hk_app_capability_grant_t *grant = &ctx->capabilities[index];
        if(grant->id == id && grant->instance == instance)
        {
            *available = grant->available;
            *fallback = grant->fallback;
            return HK_OK;
        }
    }
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t context_capability(
    const hk_app_context_t *ctx,
    hk_capability_id_t id,
    uint16_t instance,
    hk_lease_t *lease)
{
    uint16_t index;
    hk_result_t result;

    if(!lease)
        return HK_ERR_INVALID_ARGUMENT;
    *lease = HK_LEASE_NONE;
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < ctx->capability_count; index++)
    {
        const hk_app_capability_grant_t *grant = &ctx->capabilities[index];
        if(grant->id != id || grant->instance != instance)
            continue;
        if(!grant->available)
            return HK_ERR_CAPABILITY_ABSENT;
        if(hk_owner_is_zero(ctx->owner) ||
           !lease_record(s_active_fake, &grant->lease))
            return HK_ERR_INVALID_STATE;
        *lease = grant->lease;
        return HK_OK;
    }
    return HK_ERR_NOT_DECLARED;
}

#define HK_FAKE_CONTEXT_ACCESSOR(function_name, type_name, capability_id)     \
    hk_result_t function_name(                                                \
        const hk_app_context_t *ctx, uint16_t instance, type_name *handle)    \
    {                                                                         \
        if(!handle)                                                           \
            return HK_ERR_INVALID_ARGUMENT;                                   \
        handle->lease = HK_LEASE_NONE;                                        \
        return context_capability(                                            \
            ctx, capability_id, instance, &handle->lease);                    \
    }

HK_FAKE_CONTEXT_ACCESSOR(hk_app_context_time, hk_time_t, HK_CAPABILITY_ID_TIME)
HK_FAKE_CONTEXT_ACCESSOR(hk_app_context_input, hk_input_t, HK_CAPABILITY_ID_INPUT)
HK_FAKE_CONTEXT_ACCESSOR(
    hk_app_context_display, hk_display_t, HK_CAPABILITY_ID_DISPLAY)
HK_FAKE_CONTEXT_ACCESSOR(
    hk_app_context_external_link,
    hk_external_link_t,
    HK_CAPABILITY_ID_EXTERNAL_LINK)
HK_FAKE_CONTEXT_ACCESSOR(
    hk_app_context_lights, hk_lights_t, HK_CAPABILITY_ID_LIGHTS)

#undef HK_FAKE_CONTEXT_ACCESSOR

hk_result_t hk_app_context_service(
    const hk_app_context_t *ctx,
    const char *id,
    hk_app_service_t *handle)
{
    hk_result_t result;
    uint16_t index;

    if(!id || !handle)
        return HK_ERR_INVALID_ARGUMENT;
    memset(handle, 0, sizeof(*handle));
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < ctx->service_count; index++)
    {
        if(strcmp(ctx->services[index].id, id) != 0)
            continue;
        if(hk_owner_is_zero(ctx->services[index].owner))
            return HK_ERR_INVALID_STATE;
        *handle = ctx->services[index];
        return HK_OK;
    }
    return HK_ERR_NOT_DECLARED;
}

hk_result_t hk_app_context_state(
    const hk_app_context_t *ctx,
    void **state,
    uint32_t *size_bytes)
{
    hk_result_t result;

    if(!state || !size_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    *state = s_callback_fake->config.entry->state_storage;
    *size_bytes = s_callback_fake->config.state_bytes;
    return HK_OK;
}

hk_result_t hk_app_context_teardown_deadline(
    const hk_app_context_t *ctx,
    hk_deadline_t *deadline)
{
    hk_result_t result;

    if(!deadline)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    if(!s_callback_fake->teardown_deadline_valid ||
       (s_callback_fake->state != HK_APP_HOST_FAKE_STOPPING &&
        s_callback_fake->state != HK_APP_HOST_FAKE_CLEANING))
        return HK_ERR_INVALID_STATE;
    *deadline = s_callback_fake->teardown_deadline;
    return HK_OK;
}

hk_result_t hk_app_context_request_render(
    const hk_app_context_t *ctx,
    const hk_display_rect_t *region)
{
    hk_result_t result = validate_callback_context(ctx);

    if(result != HK_OK)
        return result;
    if(s_callback_fake->state != HK_APP_HOST_FAKE_RUNNING)
        return HK_ERR_INVALID_STATE;
    if(region && (region->width == 0U || region->height == 0U))
        return HK_ERR_INVALID_ARGUMENT;
    s_callback_fake->render_pending = 1U;
    return HK_OK;
}

hk_result_t hk_app_context_wakeup_token(
    const hk_app_context_t *ctx,
    uint32_t value,
    hk_app_wakeup_token_t *token)
{
    hk_result_t result;

    if(!token)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx);
    if(result != HK_OK)
        return result;
    if(s_callback_fake->state != HK_APP_HOST_FAKE_RUNNING)
        return HK_ERR_INVALID_STATE;
    *token = (hk_app_wakeup_token_t){
        sizeof(*token), HK_APP_WAKEUP_TOKEN_VERSION, 1U,
        ctx->generation, ctx->generation, value,
    };
    return HK_OK;
}

static hk_result_t validate_surface(const hk_app_surface_t *surface)
{
    if(!surface || !surface->valid || !surface->fake ||
       surface->fake != s_callback_fake ||
       surface->context_generation != surface->fake->context_generation)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

hk_result_t hk_app_surface_get_info(
    const hk_app_surface_t *surface,
    hk_display_info_t *info)
{
    hk_result_t result;

    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_surface(surface);
    if(result != HK_OK)
        return result;
    *info = (hk_display_info_t){
        sizeof(*info), HK_DISPLAY_INFO_VERSION,
        surface->fake->config.display_width,
        surface->fake->config.display_height,
        HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_BASE,
        2U, 2U, 32U, 128U, HK_APP_MAX_INVALIDATIONS, 1U,
        1024U, 100000U, 0U,
    };
    return HK_OK;
}

hk_result_t hk_app_surface_invalidate(
    hk_app_surface_t *surface,
    const hk_display_rect_t *region)
{
    hk_result_t result = validate_surface(surface);

    if(result != HK_OK)
        return result;
    if(region && (region->width == 0U || region->height == 0U))
        return HK_ERR_INVALID_ARGUMENT;
    surface->fake->render_pending = 1U;
    return HK_OK;
}

static hk_result_t surface_operation(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect)
{
    hk_result_t result = validate_surface(surface);

    if(result != HK_OK)
        return result;
    if(rect && (rect->width == 0U || rect->height == 0U))
        return HK_ERR_INVALID_ARGUMENT;
    surface->fake->display_operations++;
    return HK_OK;
}

hk_result_t hk_app_surface_clear(hk_app_surface_t *surface, uint16_t rgb565)
{
    (void)rgb565;
    return surface_operation(surface, NULL);
}

hk_result_t hk_app_surface_fill_rect(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    (void)rgb565;
    return rect ? surface_operation(surface, rect) : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_app_surface_stroke_rect(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    return hk_app_surface_fill_rect(surface, rect, rgb565);
}

hk_result_t hk_app_surface_text(
    hk_app_surface_t *surface,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    (void)rgb565;
    if(!utf8 || size_bytes == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    return bounds ? surface_operation(surface, bounds) : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_app_surface_blit(
    hk_app_surface_t *surface,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    (void)pixel_format;
    if(!pixels || !pixels->data || pixels->size_bytes == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    return destination
               ? surface_operation(surface, destination)
               : HK_ERR_INVALID_ARGUMENT;
}

static hk_result_t acquire_capability(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_capability_id_t id,
    uint32_t display_plane,
    hk_lease_t *lease)
{
    hk_result_t result;
    uint16_t index;

    if(!lease)
        return HK_ERR_INVALID_ARGUMENT;
    *lease = HK_LEASE_NONE;
    if(!s_active_fake)
        return HK_ERR_INVALID_STATE;
    result = validate_capability_request(s_active_fake, request, id);
    if(result != HK_OK)
        return result;
    if(hk_owner_is_zero(s_active_fake->context.owner))
        return HK_ERR_STALE_HANDLE;
    if(!owner_equal(owner, s_active_fake->context.owner))
        return HK_ERR_WRONG_OWNER;
    for(index = 0U; index < s_active_fake->config.grant_count; index++)
    {
        const hk_app_capability_grant_t *grant =
            &s_active_fake->context.capabilities[index];

        if(grant->id != id || grant->instance != request->instance)
            continue;
        if(!grant->available)
            return HK_ERR_CAPABILITY_ABSENT;
        return allocate_lease(s_active_fake, id, display_plane, lease);
    }
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t release_capability(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_capability_id_t id,
    hk_lease_t *lease)
{
    hk_app_host_fake_lease_t *record;
    hk_result_t result;

    if(!lease)
        return HK_ERR_INVALID_ARGUMENT;
    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(lease))
        return HK_OK;
    result = validate_handle(owner, lease, id, NULL);
    if(result != HK_OK)
        return result;
    record = lease_record(s_active_fake, lease);
    if(!record)
        return HK_ERR_STALE_HANDLE;
    {
        uint32_t generation = record->lease.generation;

        memset(record, 0, sizeof(*record));
        record->lease.generation = generation;
    }
    *lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t hk_time_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_time_t *handle)
{
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    return acquire_capability(
        owner, request, HK_CAPABILITY_ID_TIME, 0U, &handle->lease);
}

hk_result_t hk_time_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_time_t *handle)
{
    return handle
               ? release_capability(
                     owner, deadline, HK_CAPABILITY_ID_TIME, &handle->lease)
               : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_time_now_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t *value)
{
    hk_result_t result;

    if(!handle || !value)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, &handle->lease, HK_CAPABILITY_ID_TIME, NULL);
    if(result == HK_OK)
        *value = s_active_fake->now_us;
    return result;
}

hk_result_t hk_time_deadline_after_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    hk_result_t result;

    if(!deadline)
        return HK_ERR_INVALID_ARGUMENT;
    deadline->at_us = 0U;
    result = handle
                 ? validate_handle(
                       owner, &handle->lease, HK_CAPABILITY_ID_TIME, NULL)
                 : HK_ERR_INVALID_ARGUMENT;
    if(result != HK_OK)
        return result;
    if(duration_us > HK_TIME_MAX_SLEEP_US ||
       duration_us >= UINT64_MAX - s_active_fake->now_us)
        return HK_ERR_LIMIT;
    deadline->at_us = s_active_fake->now_us + duration_us;
    return HK_OK;
}

hk_result_t hk_time_sleep_until(
    hk_owner_t owner,
    const hk_time_t *handle,
    hk_deadline_t wake_target,
    hk_deadline_t operation_deadline,
    const hk_cancel_t *cancel)
{
    hk_result_t result;
    uint64_t now;

    if(wake_target.at_us == UINT64_MAX ||
       operation_deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = handle
                 ? validate_handle(
                       owner, &handle->lease, HK_CAPABILITY_ID_TIME, NULL)
                 : HK_ERR_INVALID_ARGUMENT;
    if(result != HK_OK)
        return result;
    now = s_active_fake->now_us;
    if(now >= wake_target.at_us)
        return HK_OK;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(operation_deadline.at_us == 0U || now >= operation_deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(wake_target.at_us - now > HK_TIME_MAX_SLEEP_US ||
       operation_deadline.at_us - now > HK_TIME_MAX_SLEEP_US)
        return HK_ERR_LIMIT;
    while(1)
    {
        uint64_t stop_at = wake_target.at_us < operation_deadline.at_us ?
            wake_target.at_us : operation_deadline.at_us;
        uint64_t slice_us = stop_at - now;

        if(slice_us > HK_TIME_CANCEL_PROBE_MAX_US)
            slice_us = HK_TIME_CANCEL_PROBE_MAX_US;
        if(slice_us == 0U)
            return HK_ERR_INTERNAL;
        now += slice_us;
        s_active_fake->now_us = now;
        if(now >= wake_target.at_us)
            return HK_OK;
        if(cancel && cancel->probe && cancel->probe(cancel->context))
            return HK_ERR_CANCELLED;
        if(now >= operation_deadline.at_us)
            return HK_ERR_DEADLINE_EXCEEDED;
    }
}

hk_result_t hk_input_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_input_t *handle)
{
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    return acquire_capability(
        owner, request, HK_CAPABILITY_ID_INPUT, 0U, &handle->lease);
}

hk_result_t hk_input_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_input_t *handle)
{
    return handle
               ? release_capability(
                     owner, deadline, HK_CAPABILITY_ID_INPUT, &handle->lease)
               : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_input_get_info(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_info_t *info)
{
    hk_result_t result;

    if(!handle || !info || info->struct_size < sizeof(*info))
        return HK_ERR_INVALID_ARGUMENT;
    if(info->struct_version != HK_INPUT_INFO_VERSION)
        return HK_ERR_VERSION_INCOMPATIBLE;
    result = validate_handle(owner, &handle->lease, HK_CAPABILITY_ID_INPUT, NULL);
    if(result != HK_OK)
        return result;
    *info = (hk_input_info_t){
        sizeof(*info), HK_INPUT_INFO_VERSION, HK_INPUT_BUTTON_ALL,
        HK_INPUT_SAMPLE_INTERVAL_US, HK_INPUT_DEBOUNCE_INTERVAL_US,
        HK_APP_HOST_FAKE_INPUT_CAPACITY, 0U,
    };
    return HK_OK;
}

hk_result_t hk_input_get_state(
    hk_owner_t owner,
    const hk_input_t *handle,
    uint32_t *state)
{
    hk_result_t result;

    if(!handle || !state)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, &handle->lease, HK_CAPABILITY_ID_INPUT, NULL);
    if(result == HK_OK)
        *state = s_active_fake->input_state;
    return result;
}

hk_result_t hk_input_next_event(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_event_t *event)
{
    hk_app_host_fake_lease_t *record;
    uint64_t oldest;
    hk_result_t result;

    if(!handle || !event)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, &handle->lease, HK_CAPABILITY_ID_INPUT, NULL);
    if(result != HK_OK)
        return result;
    memset(event, 0, sizeof(*event));
    record = lease_record(s_active_fake, &handle->lease);
    if(!record)
        return HK_ERR_STALE_HANDLE;
    if(record->input_next_sequence > s_active_fake->input_sequence)
        return HK_PENDING;
    oldest = s_active_fake->input_sequence >= HK_APP_HOST_FAKE_INPUT_CAPACITY ?
        s_active_fake->input_sequence - HK_APP_HOST_FAKE_INPUT_CAPACITY + 1U :
        1U;
    if(record->input_next_sequence < oldest)
    {
        uint64_t dropped =
            s_active_fake->input_sequence - record->input_next_sequence + 1U;

        event->sequence = s_active_fake->input_sequence;
        event->timestamp_us = s_active_fake->input_events[
            (s_active_fake->input_sequence - 1U) %
            HK_APP_HOST_FAKE_INPUT_CAPACITY].timestamp_us;
        event->state = s_active_fake->input_state;
        event->dropped = dropped > UINT32_MAX ? UINT32_MAX : (uint32_t)dropped;
        record->input_next_sequence = s_active_fake->input_sequence + 1U;
        return HK_ERR_OVERFLOW;
    }
    *event = s_active_fake->input_events[
        (record->input_next_sequence - 1U) %
        HK_APP_HOST_FAKE_INPUT_CAPACITY];
    record->input_next_sequence++;
    return HK_OK;
}

hk_result_t hk_display_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint32_t plane,
    hk_display_t *handle)
{
    if(!handle ||
       (plane != HK_DISPLAY_PLANE_BASE && plane != HK_DISPLAY_PLANE_OVERLAY))
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    if(plane == HK_DISPLAY_PLANE_OVERLAY)
        return HK_ERR_FEATURE_UNAVAILABLE;
    return acquire_capability(
        owner, request, HK_CAPABILITY_ID_DISPLAY, plane, &handle->lease);
}

hk_result_t hk_display_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_display_t *handle)
{
    return handle
               ? release_capability(
                     owner, deadline, HK_CAPABILITY_ID_DISPLAY, &handle->lease)
               : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_display_get_info(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_display_info_t *info)
{
    hk_result_t result;

    if(!handle || !info)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, &handle->lease, HK_CAPABILITY_ID_DISPLAY, NULL);
    if(result != HK_OK)
        return result;
    *info = (hk_display_info_t){
        sizeof(*info), HK_DISPLAY_INFO_VERSION,
        s_active_fake->config.display_width,
        s_active_fake->config.display_height,
        HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_BASE,
        2U, 2U, HK_APP_HOST_FAKE_DISPLAY_MAX_COMMANDS,
        HK_APP_HOST_FAKE_DISPLAY_MAX_TEXT_BYTES,
        HK_APP_HOST_FAKE_DISPLAY_MAX_DIRTY_RECTS,
        s_active_fake->config.display_surface.data ? 1U : 0U,
        1024U, HK_APP_HOST_FAKE_DISPLAY_MAX_PRESENT_US, 0U,
    };
    return HK_OK;
}

static hk_result_t display_record(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_app_host_fake_lease_t **record)
{
    hk_result_t result;

    if(!handle || !record)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(
        owner, &handle->lease, HK_CAPABILITY_ID_DISPLAY, NULL);
    if(result != HK_OK)
        return result;
    *record = lease_record(s_active_fake, &handle->lease);
    return *record ? HK_OK : HK_ERR_STALE_HANDLE;
}

static hk_result_t clip_rect(
    const hk_app_host_fake_lease_t *record,
    const hk_display_rect_t *rect,
    hk_display_rect_t *clipped,
    uint8_t *visible)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t clip_right;
    int64_t clip_bottom;

    if(!record || !rect || !clipped || !visible)
        return HK_ERR_INVALID_ARGUMENT;
    *visible = 0U;
    memset(clipped, 0, sizeof(*clipped));
    if(rect->width == 0U || rect->height == 0U)
        return HK_OK;
    right = (int64_t)rect->x + rect->width;
    bottom = (int64_t)rect->y + rect->height;
    if(right > INT32_MAX || bottom > INT32_MAX ||
       right < INT32_MIN || bottom < INT32_MIN)
        return HK_ERR_INVALID_ARGUMENT;
    clip_right = (int64_t)record->display_clip.x +
        record->display_clip.width;
    clip_bottom = (int64_t)record->display_clip.y +
        record->display_clip.height;
    left = rect->x > record->display_clip.x ?
        rect->x : record->display_clip.x;
    top = rect->y > record->display_clip.y ?
        rect->y : record->display_clip.y;
    if(right > clip_right)
        right = clip_right;
    if(bottom > clip_bottom)
        bottom = clip_bottom;
    if(right <= left || bottom <= top)
        return HK_OK;
    *clipped = (hk_display_rect_t){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top),
    };
    *visible = 1U;
    return HK_OK;
}

static void reset_display_stage(hk_app_host_fake_lease_t *record)
{
    record->display_state = HK_APP_HOST_FAKE_DISPLAY_IDLE;
    record->display_commands = 0U;
    record->display_text_bytes = 0U;
    record->display_dirty_rects = 0U;
    record->display_clip = (hk_display_rect_t){
        0, 0, s_active_fake->config.display_width,
        s_active_fake->config.display_height,
    };
}

static hk_result_t stage_rect_command(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t text_bytes)
{
    hk_app_host_fake_lease_t *record;
    hk_display_rect_t clipped;
    hk_result_t result = display_record(owner, handle, &record);
    uint8_t visible;

    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_BATCH)
        return HK_ERR_INVALID_STATE;
    result = clip_rect(record, rect, &clipped, &visible);
    if(result != HK_OK || !visible)
        return result;
    if(record->display_commands >= HK_APP_HOST_FAKE_DISPLAY_MAX_COMMANDS ||
       record->display_dirty_rects >=
           HK_APP_HOST_FAKE_DISPLAY_MAX_DIRTY_RECTS ||
       text_bytes > HK_APP_HOST_FAKE_DISPLAY_MAX_TEXT_BYTES -
           record->display_text_bytes)
        return HK_ERR_LIMIT;
    record->display_commands++;
    record->display_dirty_rects++;
    record->display_text_bytes =
        (uint16_t)(record->display_text_bytes + text_bytes);
    s_active_fake->display_operations++;
    return HK_OK;
}

hk_result_t hk_display_begin_batch(
    hk_owner_t owner,
    const hk_display_t *handle)
{
    hk_app_host_fake_lease_t *record;
    hk_result_t result = display_record(owner, handle, &record);

    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_IDLE)
        return HK_ERR_INVALID_STATE;
    reset_display_stage(record);
    record->display_state = HK_APP_HOST_FAKE_DISPLAY_BATCH;
    return HK_OK;
}

hk_result_t hk_display_set_clip(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *clip)
{
    hk_app_host_fake_lease_t *record;
    hk_app_host_fake_lease_t validation;
    hk_display_rect_t full;
    hk_display_rect_t clipped;
    hk_result_t result = display_record(owner, handle, &record);
    uint8_t visible;

    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_BATCH)
        return HK_ERR_INVALID_STATE;
    full = (hk_display_rect_t){
        0, 0, s_active_fake->config.display_width,
        s_active_fake->config.display_height,
    };
    if(!clip)
    {
        record->display_clip = full;
        return HK_OK;
    }
    validation = *record;
    validation.display_clip = full;
    result = clip_rect(&validation, clip, &clipped, &visible);
    if(result != HK_OK)
        return result;
    record->display_clip = visible ? clipped :
        (hk_display_rect_t){0, 0, 0U, 0U};
    return HK_OK;
}

hk_result_t hk_display_clear(
    hk_owner_t owner,
    const hk_display_t *handle,
    uint16_t rgb565)
{
    hk_app_host_fake_lease_t *record;
    hk_result_t result = display_record(owner, handle, &record);

    (void)rgb565;
    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_BATCH)
        return HK_ERR_INVALID_STATE;
    if(record->display_commands >= HK_APP_HOST_FAKE_DISPLAY_MAX_COMMANDS ||
       record->display_dirty_rects >=
           HK_APP_HOST_FAKE_DISPLAY_MAX_DIRTY_RECTS)
        return HK_ERR_LIMIT;
    record->display_commands++;
    record->display_dirty_rects++;
    s_active_fake->display_operations++;
    return HK_OK;
}

hk_result_t hk_display_fill_rect(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    (void)rgb565;
    return rect ? stage_rect_command(owner, handle, rect, 0U) :
        HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_display_stroke_rect(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    return hk_display_fill_rect(owner, handle, rect, rgb565);
}

hk_result_t hk_display_text(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    (void)rgb565;
    if(!bounds || !utf8 || size_bytes == 0U || size_bytes > UINT16_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    return stage_rect_command(owner, handle, bounds, (uint16_t)size_bytes);
}

hk_result_t hk_display_blit(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    hk_app_host_fake_lease_t *record;
    hk_display_rect_t clipped;
    hk_result_t result;
    uint8_t visible;
    uint64_t required;

    if(!destination)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_record(owner, handle, &record);
    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_BATCH)
        return HK_ERR_INVALID_STATE;
    result = clip_rect(record, destination, &clipped, &visible);
    if(result != HK_OK || !visible)
        return result;
    if(!pixels || !pixels->data ||
       pixel_format != HK_DISPLAY_FORMAT_RGB565_BE ||
       (pixels->flags & HK_BUFFER_ACCESS_READABLE) == 0U ||
       (((uintptr_t)pixels->data) & 1U) != 0U ||
       pixels->stride_bytes < (uint64_t)destination->width * 2U)
        return HK_ERR_INVALID_ARGUMENT;
    required = (uint64_t)pixels->stride_bytes * destination->height;
    if(required > pixels->size_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    return stage_rect_command(owner, handle, destination, 0U);
}

hk_result_t hk_display_mark_dirty(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect)
{
    hk_app_host_fake_lease_t *record;
    hk_display_rect_t clipped;
    hk_result_t result;
    uint8_t visible;

    if(!rect)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_record(owner, handle, &record);
    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_BATCH &&
       record->display_state != HK_APP_HOST_FAKE_DISPLAY_SURFACE)
        return HK_ERR_INVALID_STATE;
    result = clip_rect(record, rect, &clipped, &visible);
    if(result != HK_OK || !visible)
        return result;
    if(record->display_dirty_rects >=
       HK_APP_HOST_FAKE_DISPLAY_MAX_DIRTY_RECTS)
        return HK_ERR_LIMIT;
    record->display_dirty_rects++;
    s_active_fake->display_operations++;
    return HK_OK;
}

hk_result_t hk_display_surface_acquire(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_display_surface_t *surface)
{
    hk_app_host_fake_lease_t *record;
    hk_result_t result;

    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_record(owner, handle, &record);
    if(result != HK_OK)
        return result;
    if(record->display_state != HK_APP_HOST_FAKE_DISPLAY_IDLE)
        return HK_ERR_INVALID_STATE;
    if(!s_active_fake->config.display_surface.data)
        return HK_ERR_FEATURE_UNAVAILABLE;
    reset_display_stage(record);
    record->display_state = HK_APP_HOST_FAKE_DISPLAY_SURFACE;
    memset(surface, 0, sizeof(*surface));
    surface->struct_size = sizeof(*surface);
    surface->struct_version = HK_DISPLAY_SURFACE_VERSION;
    surface->width = s_active_fake->config.display_width;
    surface->height = s_active_fake->config.display_height;
    surface->pixel_format = HK_DISPLAY_FORMAT_RGB565_BE;
    surface->pixels = s_active_fake->config.display_surface;
    return HK_OK;
}

hk_result_t hk_display_present(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    hk_app_host_fake_lease_t *record;
    hk_result_t result;

    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_record(owner, handle, &record);
    if(result != HK_OK)
        return result;
    if(record->display_state == HK_APP_HOST_FAKE_DISPLAY_IDLE)
        return HK_ERR_INVALID_STATE;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us == 0U || deadline.at_us <= s_active_fake->now_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    reset_display_stage(record);
    s_active_fake->display_present_calls++;
    return HK_OK;
}

hk_result_t hk_display_abort(
    hk_owner_t owner,
    const hk_display_t *handle)
{
    hk_app_host_fake_lease_t *record;
    hk_result_t result = display_record(owner, handle, &record);

    if(result != HK_OK)
        return result;
    if(record->display_state == HK_APP_HOST_FAKE_DISPLAY_IDLE)
        return HK_ERR_INVALID_STATE;
    reset_display_stage(record);
    return HK_OK;
}
