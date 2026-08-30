#include "runtime_private.h"

#include <limits.h>
#include <string.h>

static hk_app_runtime_t *s_live_runtime;
static hk_app_runtime_t *s_callback_runtime;

static uint8_t is_terminal_failure(hk_result_t result)
{
    return (uint8_t)(result != HK_OK);
}

static hk_result_t callback_result(hk_result_t result)
{
    return result == HK_PENDING ? HK_ERR_INVALID_STATE : result;
}

static void retain_error(hk_app_runtime_t *runtime, hk_result_t result)
{
    result = callback_result(result);
    if(is_terminal_failure(result) && runtime->first_error == HK_OK)
        runtime->first_error = result;
}

static hk_result_t validate_descriptor(const hk_app_t *descriptor)
{
    const hk_app_v2_entry_t *entry;
    uintptr_t state_address;
    uint16_t index;

    if(!descriptor || descriptor->struct_size != sizeof(hk_app_t) ||
       descriptor->struct_version != HK_APP_DESCRIPTOR_VERSION ||
       descriptor->lifecycle != HK_APP_LIFECYCLE_V2 ||
       !descriptor->entry.v2 || !descriptor->id)
        return HK_ERR_INVALID_ARGUMENT;
    entry = descriptor->entry.v2;
    if(!entry->state_storage || descriptor->limits.static_ram_bytes == 0U ||
       descriptor->limits.stack_bytes == 0U ||
       descriptor->limits.state_bytes == 0U ||
       descriptor->limits.state_bytes > descriptor->limits.static_ram_bytes ||
       descriptor->limits.tick_interval_us == 0U ||
       descriptor->limits.tick_budget_us == 0U ||
       descriptor->limits.tick_budget_us > descriptor->limits.tick_interval_us ||
       descriptor->limits.render_budget_us == 0U ||
       entry->state_capacity_bytes < descriptor->limits.state_bytes ||
       descriptor->limits.state_alignment != HK_APP_STATE_ALIGNMENT ||
       descriptor->capability_count > HK_APP_CONTEXT_MAX_CAPABILITIES ||
       descriptor->service_count > HK_APP_CONTEXT_MAX_SERVICES ||
       (descriptor->capability_count > 0U && !descriptor->capabilities) ||
       (descriptor->service_count > 0U && !descriptor->services) ||
       !entry->probe || !entry->prepare || !entry->start || !entry->event ||
       !entry->tick || !entry->render || !entry->stop || !entry->cleanup)
        return HK_ERR_INVALID_ARGUMENT;
    for(index = 0U; index < descriptor->capability_count; index++)
    {
        const hk_app_capability_request_t *request =
            &descriptor->capabilities[index];
        uint16_t previous;

        if(!request->id || !request->minimum || !request->maximum_exclusive ||
           (request->feature_count > 0U && !request->features) ||
           (request->optional && !request->fallback) ||
           (!request->optional && request->fallback))
            return HK_ERR_INVALID_ARGUMENT;
        for(previous = 0U; previous < index; previous++)
        {
            const hk_app_capability_request_t *candidate =
                &descriptor->capabilities[previous];
            if(candidate->instance == request->instance &&
               strcmp(candidate->id, request->id) == 0)
                return HK_ERR_INVALID_ARGUMENT;
        }
    }
    for(index = 0U; index < descriptor->service_count; index++)
    {
        const hk_app_service_request_t *service = &descriptor->services[index];
        uint16_t previous;

        if(!service->id || !service->namespace_name)
            return HK_ERR_INVALID_ARGUMENT;
        for(previous = 0U; previous < index; previous++)
        {
            const hk_app_service_request_t *candidate =
                &descriptor->services[previous];
            if(strcmp(candidate->id, service->id) == 0 ||
               strcmp(candidate->namespace_name, service->namespace_name) == 0)
                return HK_ERR_INVALID_ARGUMENT;
        }
    }
    state_address = (uintptr_t)entry->state_storage;
    if((state_address % descriptor->limits.state_alignment) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

static hk_result_t enter_callback(hk_app_runtime_t *runtime)
{
    if(runtime->callback_active || s_callback_runtime)
        return HK_ERR_INVALID_STATE;
    runtime->callback_active = 1U;
    s_callback_runtime = runtime;
    return HK_OK;
}

static hk_result_t finish_callback(
    hk_app_runtime_t *runtime,
    hk_result_t result)
{
    runtime->callback_active = 0U;
    if(s_callback_runtime == runtime)
        s_callback_runtime = NULL;
    return callback_result(result);
}

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t request_is_valid(const hk_capability_request_t *request)
{
    if(!request || request->struct_size < sizeof(*request) ||
       request->struct_version != HK_CAPABILITY_REQUEST_VERSION ||
       request->id == 0U || request->reserved != 0U ||
       request->minimum.reserved != 0U ||
       request->maximum_exclusive.reserved != 0U)
        return 0U;
    if(request->minimum.major > request->maximum_exclusive.major)
        return 0U;
    if(request->minimum.major == request->maximum_exclusive.major &&
       request->minimum.minor > request->maximum_exclusive.minor)
        return 0U;
    if(request->minimum.major == request->maximum_exclusive.major &&
       request->minimum.minor == request->maximum_exclusive.minor &&
       request->minimum.patch >= request->maximum_exclusive.patch)
        return 0U;
    return 1U;
}

static uint8_t is_optional_absence(hk_result_t result)
{
    return (uint8_t)(result == HK_ERR_CAPABILITY_ABSENT ||
                     result == HK_ERR_VERSION_INCOMPATIBLE ||
                     result == HK_ERR_FEATURE_UNAVAILABLE ||
                     result == HK_ERR_NOT_DECLARED);
}

static hk_result_t resolve_declared_surface(hk_app_runtime_t *runtime)
{
    const hk_app_t *descriptor = runtime->descriptor;
    uint16_t index;

    runtime->context.capability_count = descriptor->capability_count;
    runtime->context.service_count = descriptor->service_count;
    for(index = 0U; index < descriptor->capability_count; index++)
    {
        const hk_app_capability_request_t *declaration =
            &descriptor->capabilities[index];
        hk_app_capability_grant_t *grant =
            &runtime->context.capabilities[index];
        hk_capability_request_t *request =
            &runtime->resolved_capabilities[index];
        hk_result_t result;

        memset(request, 0, sizeof(*request));
        grant->fallback = declaration->fallback;
        grant->instance = declaration->instance;
        grant->optional = declaration->optional;
        result = runtime->ops.resolve_capability(
            runtime->ops.user, descriptor, declaration, request);
        if(!request_is_valid(request) || request->instance != declaration->instance)
            return HK_ERR_INTERNAL;
        grant->id = request->id;
        if(result == HK_OK)
        {
            grant->available = 1U;
            runtime->resolved_available[index] = 1U;
            continue;
        }
        if(declaration->optional && is_optional_absence(result))
        {
            grant->available = 0U;
            runtime->resolved_available[index] = 0U;
            continue;
        }
        return result;
    }
    for(index = 0U; index < descriptor->service_count; index++)
    {
        const hk_app_service_request_t *declaration =
            &descriptor->services[index];
        hk_result_t result = runtime->ops.resolve_service(
            runtime->ops.user, descriptor, declaration);

        runtime->context.services[index].id = declaration->id;
        runtime->context.services[index].namespace_name =
            declaration->namespace_name;
        if(result != HK_OK)
            return result;
    }
    return HK_OK;
}

static hk_result_t inject_declared_surface(hk_app_runtime_t *runtime)
{
    uint16_t index;

    for(index = 0U; index < runtime->descriptor->capability_count; index++)
    {
        hk_app_capability_grant_t *grant =
            &runtime->context.capabilities[index];
        hk_lease_t lease = HK_LEASE_NONE;
        hk_result_t result;

        if(!runtime->resolved_available[index])
            continue;
        result = runtime->ops.acquire_capability(
            runtime->ops.user, runtime->owner,
            &runtime->resolved_capabilities[index], &lease);
        if(result != HK_OK)
            return result;
        if(hk_lease_is_zero(&lease) ||
           !owner_equal(lease.owner, runtime->owner) ||
           lease.capability_id != grant->id)
            return HK_ERR_INTERNAL;
        grant->lease = lease;
    }
    for(index = 0U; index < runtime->descriptor->service_count; index++)
    {
        const hk_app_service_request_t *declaration =
            &runtime->descriptor->services[index];
        hk_result_t result = runtime->ops.acquire_service(
            runtime->ops.user, runtime->owner, declaration);
        hk_app_service_t *handle = &runtime->context.services[index];

        if(result != HK_OK)
            return result;
        handle->owner = runtime->owner;
        handle->context_generation = runtime->context.generation;
    }
    return HK_OK;
}

static hk_app_stop_reason_t normalized_reason(hk_app_stop_reason_t reason)
{
    if((unsigned)reason > (unsigned)HK_APP_STOP_SHUTDOWN)
        return HK_APP_STOP_FORCED;
    return reason;
}

static void invalidate_instance(hk_app_runtime_t *runtime)
{
    const hk_app_v2_entry_t *entry = runtime->descriptor->entry.v2;
    uint8_t exhausted = 0U;

    runtime->stage = HK_APP_STAGE_INVALIDATING;
    runtime->context_valid = 0U;
    runtime->teardown_deadline_valid = 0U;
    runtime->owner = HK_OWNER_NONE;
    runtime->context.owner = HK_OWNER_NONE;
    memset(entry->state_storage, 0, runtime->descriptor->limits.state_bytes);
    if(runtime->context_generation == UINT32_MAX ||
       runtime->active_epoch == UINT32_MAX)
    {
        exhausted = 1U;
    }
    else
    {
        runtime->context_generation++;
        runtime->active_epoch++;
    }
    runtime->descriptor = NULL;
    memset(runtime->resolved_capabilities, 0,
           sizeof(runtime->resolved_capabilities));
    memset(runtime->resolved_available, 0,
           sizeof(runtime->resolved_available));
    memset(&runtime->context, 0, sizeof(runtime->context));
    runtime->teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->prepare_entered = 0U;
    runtime->start_entered = 0U;
    runtime->stop_called = 0U;
    runtime->cleanup_called = 0U;
    runtime->teardown_started = 0U;
    runtime->callback_active = 0U;
    if(s_callback_runtime == runtime)
        s_callback_runtime = NULL;
    if(s_live_runtime == runtime)
        s_live_runtime = NULL;
    runtime->retired = exhausted;
    runtime->stage = exhausted ? HK_APP_STAGE_INVALIDATING : HK_APP_STAGE_REUSABLE;
    runtime->state = exhausted ? HK_APP_RUNTIME_FAULTED : HK_APP_RUNTIME_INACTIVE;
}

static hk_result_t teardown(
    hk_app_runtime_t *runtime,
    hk_app_stop_reason_t reason)
{
    const hk_app_v2_entry_t *entry;
    hk_result_t result;

    if(!runtime || !runtime->descriptor)
        return HK_ERR_INVALID_STATE;
    if(runtime->callback_active || runtime->teardown_started)
        return HK_ERR_INVALID_STATE;

    runtime->teardown_started = 1U;
    runtime->stop_reason = normalized_reason(reason);
    entry = runtime->descriptor->entry.v2;

    if(runtime->active_epoch != UINT32_MAX)
        runtime->active_epoch++;
    else
        runtime->retired = 1U;

    runtime->teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->teardown_deadline_valid = 1U;
    result = runtime->ops.deadline_after_us(
        runtime->ops.user,
        runtime->teardown_budget_us,
        &runtime->teardown_deadline);
    if(result == HK_OK &&
       runtime->teardown_deadline.at_us == UINT64_MAX)
        result = HK_ERR_INTERNAL;
    if(result != HK_OK)
    {
        runtime->teardown_deadline = HK_DEADLINE_IMMEDIATE;
        retain_error(runtime, result);
    }

    if(runtime->start_entered && !runtime->stop_called)
    {
        runtime->state = HK_APP_RUNTIME_STOPPING;
        runtime->stage = HK_APP_STAGE_STOPPING;
        runtime->stop_called = 1U;
        if(enter_callback(runtime) == HK_OK)
        {
            result = finish_callback(
                runtime,
                entry->stop(&runtime->context, runtime->stop_reason));
            retain_error(runtime, result);
        }
    }

    runtime->state = HK_APP_RUNTIME_CLEANING;
    if(runtime->prepare_entered && !runtime->cleanup_called)
    {
        runtime->stage = HK_APP_STAGE_APP_CLEANUP;
        runtime->cleanup_called = 1U;
        if(enter_callback(runtime) == HK_OK)
        {
            result = finish_callback(runtime, entry->cleanup(&runtime->context));
            retain_error(runtime, result);
        }
    }

    if(!hk_owner_is_zero(runtime->owner))
    {
        runtime->stage = HK_APP_STAGE_OWNER_CLEANUP;
        result = runtime->ops.owner_cleanup(
            runtime->ops.user,
            runtime->owner,
            runtime->teardown_deadline);
        retain_error(runtime, result);
    }

    result = runtime->first_error;
    invalidate_instance(runtime);
    return result;
}

static hk_result_t fail_without_teardown(
    hk_app_runtime_t *runtime,
    hk_result_t error)
{
    retain_error(runtime, error);
    invalidate_instance(runtime);
    return runtime->first_error;
}

hk_result_t hk_app_runtime_init(
    hk_app_runtime_t *runtime,
    const hk_app_runtime_ops_t *ops,
    uint64_t teardown_budget_us)
{
    if(!runtime || !ops || !ops->resolve_capability ||
       !ops->resolve_service || !ops->owner_open ||
       !ops->acquire_capability || !ops->acquire_service ||
       !ops->owner_cleanup ||
       !ops->deadline_after_us || teardown_budget_us == 0U ||
       teardown_budget_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    memset(runtime, 0, sizeof(*runtime));
    runtime->ops = *ops;
    runtime->teardown_budget_us = teardown_budget_us;
    runtime->context_generation = 1U;
    runtime->active_epoch = 1U;
    runtime->state = HK_APP_RUNTIME_INACTIVE;
    runtime->stage = HK_APP_STAGE_REUSABLE;
    return HK_OK;
}

hk_result_t hk_app_runtime_launch(
    hk_app_runtime_t *runtime,
    const hk_app_t *descriptor)
{
    const hk_app_v2_entry_t *entry;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_result_t result;

    if(!runtime || runtime->retired || runtime->state == HK_APP_RUNTIME_FAULTED)
        return HK_ERR_INVALID_STATE;
    if(runtime->state != HK_APP_RUNTIME_INACTIVE || runtime->callback_active ||
       (s_live_runtime && s_live_runtime != runtime))
        return HK_ERR_BUSY;
    result = validate_descriptor(descriptor);
    if(result != HK_OK)
        return result;

    runtime->descriptor = descriptor;
    s_live_runtime = runtime;
    runtime->first_error = HK_OK;
    runtime->stop_reason = HK_APP_STOP_COMPLETED;
    memset(&runtime->context, 0, sizeof(runtime->context));
    runtime->context.struct_size = sizeof(runtime->context);
    runtime->context.struct_version = HK_APP_CONTEXT_VERSION;
    runtime->context.app_id = descriptor->id;
    runtime->owner = HK_OWNER_NONE;
    runtime->context.owner = HK_OWNER_NONE;
    runtime->context.generation = runtime->context_generation;
    runtime->context_valid = 1U;
    runtime->teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->teardown_deadline_valid = 0U;
    entry = descriptor->entry.v2;
    memset(entry->state_storage, 0, descriptor->limits.state_bytes);

    result = resolve_declared_surface(runtime);
    if(result != HK_OK)
        return fail_without_teardown(runtime, result);

    runtime->stage = HK_APP_STAGE_PROBING;
    if(enter_callback(runtime) != HK_OK)
        return fail_without_teardown(runtime, HK_ERR_INVALID_STATE);
    result = finish_callback(runtime, entry->probe(&runtime->context));
    if(result != HK_OK)
        return fail_without_teardown(runtime, result);
    runtime->state = HK_APP_RUNTIME_PROBED;

    runtime->stage = HK_APP_STAGE_INJECTING;
    runtime->state = HK_APP_RUNTIME_INJECTING;
    result = runtime->ops.owner_open(runtime->ops.user, descriptor, &owner);
    runtime->owner = owner;
    runtime->context.owner = owner;
    if(result != HK_OK)
    {
        retain_error(runtime, result);
        if(hk_owner_is_zero(owner))
            return fail_without_teardown(runtime, result);
        return teardown(runtime, HK_APP_STOP_FORCED);
    }
    if(hk_owner_is_zero(owner))
    {
        retain_error(runtime, HK_ERR_INTERNAL);
        return fail_without_teardown(runtime, HK_ERR_INTERNAL);
    }
    result = inject_declared_surface(runtime);
    if(result != HK_OK)
    {
        retain_error(runtime, result);
        return teardown(runtime, HK_APP_STOP_FORCED);
    }

    runtime->stage = HK_APP_STAGE_PREPARING;
    runtime->prepare_entered = 1U;
    if(enter_callback(runtime) != HK_OK)
        return teardown(runtime, HK_APP_STOP_FORCED);
    result = finish_callback(runtime, entry->prepare(&runtime->context));
    if(result != HK_OK)
    {
        retain_error(runtime, result);
        return teardown(runtime, HK_APP_STOP_FORCED);
    }
    runtime->state = HK_APP_RUNTIME_PREPARED;

    runtime->stage = HK_APP_STAGE_STARTING;
    runtime->start_entered = 1U;
    if(enter_callback(runtime) != HK_OK)
        return teardown(runtime, HK_APP_STOP_START_FAILED);
    result = finish_callback(runtime, entry->start(&runtime->context));
    if(result != HK_OK)
    {
        retain_error(runtime, result);
        return teardown(runtime, HK_APP_STOP_START_FAILED);
    }
    runtime->stage = HK_APP_STAGE_RUNNING;
    runtime->state = HK_APP_RUNTIME_RUNNING;
    return HK_OK;
}

hk_result_t hk_app_runtime_stop(
    hk_app_runtime_t *runtime,
    hk_app_stop_reason_t reason)
{
    if(!runtime)
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->callback_active || runtime->teardown_started)
        return HK_ERR_INVALID_STATE;
    if(runtime->state == HK_APP_RUNTIME_INACTIVE)
        return HK_OK;
    if(runtime->state != HK_APP_RUNTIME_RUNNING || !runtime->descriptor)
        return HK_ERR_INVALID_STATE;
    return teardown(runtime, reason);
}

static hk_result_t dispatch_result(
    hk_app_runtime_t *runtime,
    hk_result_t callback_status)
{
    hk_result_t result = finish_callback(runtime, callback_status);

    if(result == HK_OK)
        return HK_OK;
    retain_error(runtime, result);
    (void)teardown(runtime, HK_APP_STOP_CALLBACK_FAILED);
    return result;
}

hk_result_t hk_app_runtime_event(
    hk_app_runtime_t *runtime,
    const hk_app_runtime_event_t *event)
{
    if(!runtime || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->state != HK_APP_RUNTIME_RUNNING || runtime->callback_active)
        return HK_ERR_INVALID_STATE;
    runtime->stage = HK_APP_STAGE_RUNNING;
    (void)enter_callback(runtime);
    return dispatch_result(
        runtime, runtime->descriptor->entry.v2->event(&runtime->context, event));
}

hk_result_t hk_app_runtime_tick(hk_app_runtime_t *runtime, uint64_t now_us)
{
    if(!runtime)
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->state != HK_APP_RUNTIME_RUNNING || runtime->callback_active)
        return HK_ERR_INVALID_STATE;
    runtime->stage = HK_APP_STAGE_RUNNING;
    (void)enter_callback(runtime);
    return dispatch_result(
        runtime, runtime->descriptor->entry.v2->tick(&runtime->context, now_us));
}

hk_result_t hk_app_runtime_render(
    hk_app_runtime_t *runtime,
    hk_app_runtime_surface_t *surface)
{
    if(!runtime || !surface)
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->state != HK_APP_RUNTIME_RUNNING || runtime->callback_active)
        return HK_ERR_INVALID_STATE;
    runtime->stage = HK_APP_STAGE_RUNNING;
    (void)enter_callback(runtime);
    return dispatch_result(
        runtime, runtime->descriptor->entry.v2->render(&runtime->context, surface));
}

hk_app_runtime_state_t hk_app_runtime_state(const hk_app_runtime_t *runtime)
{
    return runtime ? runtime->state : HK_APP_RUNTIME_FAULTED;
}

hk_app_runtime_stage_t hk_app_runtime_stage(const hk_app_runtime_t *runtime)
{
    return runtime ? runtime->stage : HK_APP_STAGE_INVALIDATING;
}

hk_result_t hk_app_runtime_first_error(const hk_app_runtime_t *runtime)
{
    return runtime ? runtime->first_error : HK_ERR_INVALID_ARGUMENT;
}

static hk_result_t validate_context(const hk_app_context_t *ctx)
{
    if(!ctx || !s_live_runtime || !s_live_runtime->context_valid ||
       ctx != &s_live_runtime->context ||
       ctx->generation != s_live_runtime->context_generation)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

static hk_result_t validate_callback_context(
    const hk_app_context_t *ctx,
    hk_app_runtime_t **runtime)
{
    hk_result_t result = validate_context(ctx);

    if(result != HK_OK)
        return result;
    if(!s_callback_runtime || s_callback_runtime != s_live_runtime ||
       !s_callback_runtime->callback_active)
        return HK_ERR_WRONG_CONTEXT;
    if(runtime)
        *runtime = s_callback_runtime;
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
    result = validate_callback_context(ctx, NULL);
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
    hk_result_t result;
    uint16_t index;

    if(id == 0U || !available || !fallback)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < ctx->capability_count; index++)
    {
        const hk_app_capability_grant_t *grant = &ctx->capabilities[index];
        if(grant->id != id || grant->instance != instance)
            continue;
        *available = grant->available;
        *fallback = grant->fallback;
        return HK_OK;
    }
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t context_capability(
    const hk_app_context_t *ctx,
    hk_capability_id_t id,
    uint16_t instance,
    hk_lease_t *lease)
{
    hk_result_t result;
    uint16_t index;

    if(!lease)
        return HK_ERR_INVALID_ARGUMENT;
    *lease = HK_LEASE_NONE;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < ctx->capability_count; index++)
    {
        const hk_app_capability_grant_t *grant = &ctx->capabilities[index];
        if(grant->id != id || grant->instance != instance)
            continue;
        if(!grant->available)
            return HK_ERR_CAPABILITY_ABSENT;
        if(hk_owner_is_zero(ctx->owner) || hk_lease_is_zero(&grant->lease))
            return HK_ERR_INVALID_STATE;
        if(!owner_equal(grant->lease.owner, ctx->owner) ||
           grant->lease.capability_id != id)
            return HK_ERR_STALE_HANDLE;
        *lease = grant->lease;
        return HK_OK;
    }
    return HK_ERR_NOT_DECLARED;
}

#define HK_APP_CONTEXT_TYPED_ACCESSOR(function_name, type_name, capability_id) \
    hk_result_t function_name(                                                \
        const hk_app_context_t *ctx, uint16_t instance, type_name *handle)    \
    {                                                                         \
        if(!handle)                                                           \
            return HK_ERR_INVALID_ARGUMENT;                                   \
        handle->lease = HK_LEASE_NONE;                                        \
        return context_capability(                                            \
            ctx, capability_id, instance, &handle->lease);                    \
    }

HK_APP_CONTEXT_TYPED_ACCESSOR(
    hk_app_context_time, hk_time_t, HK_CAPABILITY_ID_TIME)
HK_APP_CONTEXT_TYPED_ACCESSOR(
    hk_app_context_input, hk_input_t, HK_CAPABILITY_ID_INPUT)
HK_APP_CONTEXT_TYPED_ACCESSOR(
    hk_app_context_display, hk_display_t, HK_CAPABILITY_ID_DISPLAY)
HK_APP_CONTEXT_TYPED_ACCESSOR(
    hk_app_context_external_link,
    hk_external_link_t,
    HK_CAPABILITY_ID_EXTERNAL_LINK)
HK_APP_CONTEXT_TYPED_ACCESSOR(
    hk_app_context_lights, hk_lights_t, HK_CAPABILITY_ID_LIGHTS)

#undef HK_APP_CONTEXT_TYPED_ACCESSOR

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
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < ctx->service_count; index++)
    {
        const hk_app_service_t *service = &ctx->services[index];
        if(strcmp(service->id, id) != 0)
            continue;
        if(hk_owner_is_zero(service->owner))
            return HK_ERR_INVALID_STATE;
        if(!owner_equal(service->owner, ctx->owner) ||
           service->context_generation != ctx->generation)
            return HK_ERR_STALE_HANDLE;
        *handle = *service;
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
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    *state = s_callback_runtime->descriptor->entry.v2->state_storage;
    *size_bytes = s_callback_runtime->descriptor->limits.state_bytes;
    return HK_OK;
}

hk_result_t hk_app_context_teardown_deadline(
    const hk_app_context_t *ctx,
    hk_deadline_t *deadline)
{
    hk_result_t result;

    if(!deadline)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    if(!s_callback_runtime->teardown_deadline_valid ||
       (s_callback_runtime->stage != HK_APP_STAGE_STOPPING &&
        s_callback_runtime->stage != HK_APP_STAGE_APP_CLEANUP))
        return HK_ERR_INVALID_STATE;
    *deadline = s_callback_runtime->teardown_deadline;
    return HK_OK;
}

hk_result_t hk_app_context_deferred_token(
    const hk_app_context_t *ctx,
    hk_app_runtime_token_t *token)
{
    hk_result_t result;

    if(!token)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    if(s_callback_runtime->state != HK_APP_RUNTIME_RUNNING)
        return HK_ERR_INVALID_STATE;
    token->slot = HK_APP_RUNTIME_SLOT;
    token->context_generation = ctx->generation;
    token->epoch = s_callback_runtime->active_epoch;
    return HK_OK;
}

hk_result_t hk_app_runtime_validate_token(
    const hk_app_runtime_t *runtime,
    hk_app_runtime_token_t token)
{
    if(!runtime || token.slot != HK_APP_RUNTIME_SLOT ||
       runtime->state != HK_APP_RUNTIME_RUNNING || !runtime->context_valid ||
       token.context_generation != runtime->context_generation ||
       token.epoch != runtime->active_epoch)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}
