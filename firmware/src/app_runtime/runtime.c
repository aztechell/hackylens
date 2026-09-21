#include "runtime_private.h"

#include <limits.h>
#include <string.h>

_Static_assert(
    HK_APP_DESCRIPTOR_STATE_ALIGNMENT == HK_APP_STATE_ALIGNMENT,
    "generated descriptor alignment must match the public App SDK ABI");

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

static uint8_t event_is_valid(const hk_app_event_t *event)
{
    return (uint8_t)(event && event->struct_size >= sizeof(*event) &&
                     event->struct_version == HK_APP_EVENT_VERSION &&
                     event->reserved == 0U &&
                     event->kind >= HK_APP_EVENT_INPUT &&
                     event->kind <= HK_APP_EVENT_WAKEUP);
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

    if(!descriptor || descriptor->struct_size != sizeof(hk_app_t) ||
       descriptor->struct_version != HK_APP_DESCRIPTOR_VERSION ||
       !descriptor->entry || !descriptor->id)
        return HK_ERR_INVALID_ARGUMENT;
    entry = descriptor->entry;
    if(!entry->state_storage || descriptor->limits.static_ram_bytes == 0U ||
       descriptor->limits.stack_bytes == 0U ||
       descriptor->limits.state_bytes == 0U ||
       descriptor->limits.state_bytes > descriptor->limits.static_ram_bytes ||
       descriptor->limits.tick_interval_us == 0U ||
       descriptor->limits.tick_budget_us == 0U ||
       descriptor->limits.render_budget_us == 0U ||
       entry->state_capacity_bytes < descriptor->limits.state_bytes ||
       descriptor->limits.state_alignment != HK_APP_STATE_ALIGNMENT ||
       !entry->start || !entry->event || !entry->stop)
        return HK_ERR_INVALID_ARGUMENT;
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

static hk_app_stop_reason_t normalized_reason(hk_app_stop_reason_t reason)
{
    if((unsigned)reason > (unsigned)HK_APP_STOP_SHUTDOWN)
        return HK_APP_STOP_FORCED;
    return reason;
}

static void invalidate_instance(hk_app_runtime_t *runtime)
{
    const hk_app_v2_entry_t *entry = runtime->descriptor->entry;
    uint8_t exhausted = 0U;

    runtime->stage = HK_APP_STAGE_INVALIDATING;
    runtime->context_valid = 0U;
    runtime->teardown_deadline_valid = 0U;
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
    memset(runtime->invalidations, 0, sizeof(runtime->invalidations));
    memset(&runtime->context, 0, sizeof(runtime->context));
    runtime->teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->start_entered = 0U;
    runtime->stop_called = 0U;
    runtime->close_requested = 0U;
    runtime->teardown_started = 0U;
    runtime->invalidation_count = 0U;
    runtime->full_invalidation = 0U;
    runtime->callback_active = 0U;
    if(s_callback_runtime == runtime)
        s_callback_runtime = NULL;
    if(s_live_runtime == runtime)
        s_live_runtime = NULL;
    runtime->retired = exhausted;
    runtime->stage = exhausted ? HK_APP_STAGE_INVALIDATING : HK_APP_STAGE_REUSABLE;
    runtime->state = exhausted ? HK_APP_RUNTIME_FAULTED : HK_APP_RUNTIME_INACTIVE;
    runtime->close_requested = 0U;
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
    entry = runtime->descriptor->entry;

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
            result = finish_callback(runtime, entry->stop(&runtime->context));
            retain_error(runtime, result);
        }
    }

    for(unsigned i = 0U; i < 3U; ++i)
    {
        result = hk_lights_retire(&runtime->lights[i], runtime->teardown_deadline);
        retain_error(runtime, result);
    }

    for(unsigned i = 0U; i < 2U; ++i)
    {
        result = hk_display_retire(&runtime->display[i], runtime->teardown_deadline);
        retain_error(runtime, result);
    }

    result = hk_external_link_retire(&runtime->external_link, runtime->teardown_deadline);
    retain_error(runtime, result);

    runtime->state = HK_APP_RUNTIME_STOPPING;
    runtime->stage = HK_APP_STAGE_SCOPE_CLEANUP;
    result = runtime->ops.cleanup(runtime->ops.user, runtime->teardown_deadline);
    retain_error(runtime, result);

    result = runtime->first_error;
    invalidate_instance(runtime);
    return result;
}

static hk_result_t terminate_running(
    hk_app_runtime_t *runtime,
    hk_app_stop_reason_t reason)
{
    hk_app_event_t event;
    hk_result_t result;

    if(!runtime || runtime->state != HK_APP_RUNTIME_RUNNING ||
       !runtime->descriptor || runtime->callback_active ||
       runtime->teardown_started)
        return HK_ERR_INVALID_STATE;
    reason = normalized_reason(reason);
    event = (hk_app_event_t){
        sizeof(hk_app_event_t), HK_APP_EVENT_VERSION,
        HK_APP_EVENT_RUNTIME_CLOSE, 0U,
        ++runtime->event_sequence, 0U,
        {.close = {reason, 0U}},
    };
    runtime->stage = HK_APP_STAGE_RUNNING;
    result = enter_callback(runtime);
    if(result == HK_OK)
    {
        result = finish_callback(
            runtime,
            runtime->descriptor->entry->event(
                &runtime->context, &event));
    }
    retain_error(runtime, result);
    return teardown(runtime, reason);
}

hk_result_t hk_app_runtime_init(
    hk_app_runtime_t *runtime,
    const hk_app_runtime_ops_t *ops,
    uint64_t teardown_budget_us)
{
    if(!runtime || !ops || !ops->prepare || !ops->cleanup ||
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
    runtime->event_sequence = 0U;
    memset(&runtime->context, 0, sizeof(runtime->context));
    runtime->context.struct_size = sizeof(runtime->context);
    runtime->context.struct_version = HK_APP_CONTEXT_VERSION;
    runtime->context.app_id = descriptor->id;
    runtime->context.time = runtime->ops.time;
    runtime->context.input = runtime->ops.input;
    runtime->context.generation = runtime->context_generation;
    runtime->context_valid = 1U;
    runtime->teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->teardown_deadline_valid = 0U;
    entry = descriptor->entry;
    memset(entry->state_storage, 0, descriptor->limits.state_bytes);

    runtime->stage = HK_APP_STAGE_STARTING;
    runtime->state = HK_APP_RUNTIME_STARTING;
    result = callback_result(runtime->ops.prepare(runtime->ops.user, descriptor));
    if(result != HK_OK)
    {
        retain_error(runtime, result);
        return teardown(runtime, HK_APP_STOP_FORCED);
    }

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
    runtime->full_invalidation = entry->render != NULL;
    runtime->invalidation_count = 0U;
    runtime->close_requested = 0U;
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
    return terminate_running(runtime, reason);
}

static hk_result_t dispatch_result(
    hk_app_runtime_t *runtime,
    hk_result_t callback_status)
{
    hk_result_t result = finish_callback(runtime, callback_status);

    if(result != HK_OK)
    {
        retain_error(runtime, result);
        (void)terminate_running(runtime, HK_APP_STOP_CALLBACK_FAILED);
        return result;
    }
    if(runtime->close_requested)
        return terminate_running(runtime, HK_APP_STOP_COMPLETED);
    return HK_OK;
}

hk_result_t hk_app_runtime_event(
    hk_app_runtime_t *runtime,
    const hk_app_event_t *event)
{
    if(!runtime || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(!event_is_valid(event))
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->state != HK_APP_RUNTIME_RUNNING || runtime->callback_active)
        return HK_ERR_INVALID_STATE;
    runtime->stage = HK_APP_STAGE_RUNNING;
    (void)enter_callback(runtime);
    return dispatch_result(
        runtime, runtime->descriptor->entry->event(&runtime->context, event));
}

hk_result_t hk_app_runtime_render(
    hk_app_runtime_t *runtime,
    hk_app_surface_t *surface)
{
    if(!runtime || !surface)
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->state != HK_APP_RUNTIME_RUNNING || runtime->callback_active)
        return HK_ERR_INVALID_STATE;
    if(!runtime->descriptor->entry->render)
        return HK_ERR_FEATURE_UNAVAILABLE;
    runtime->stage = HK_APP_STAGE_RUNNING;
    (void)enter_callback(runtime);
    return dispatch_result(
        runtime, runtime->descriptor->entry->render(&runtime->context, surface));
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
    uint32_t *generation)
{
    hk_result_t result;

    if(!app_id || !generation)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    *app_id = ctx->app_id;
    *generation = ctx->generation;
    return HK_OK;
}

hk_result_t hk_app_context_time(
    const hk_app_context_t *ctx, const hk_time_t **time)
{
    hk_result_t result;

    if(!time)
        return HK_ERR_INVALID_ARGUMENT;
    *time = NULL;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    *time = ctx->time;
    return *time ? HK_OK : HK_ERR_CAPABILITY_ABSENT;
}
hk_result_t hk_app_context_input(
    const hk_app_context_t *ctx, const hk_input_t **input)
{
    hk_result_t result;

    if(!input)
        return HK_ERR_INVALID_ARGUMENT;
    *input = NULL;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    *input = ctx->input;
    return *input ? HK_OK : HK_ERR_CAPABILITY_ABSENT;
}
hk_result_t hk_app_context_external_link(const hk_app_context_t *ctx,
    uint64_t mode_features, hk_external_link_t **session)
{
    hk_app_runtime_t *runtime = NULL;
    hk_result_t result;
    if(!session) return HK_ERR_INVALID_ARGUMENT;
    *session = NULL;
    result = validate_callback_context(ctx, &runtime);
    if(result != HK_OK) return result;
    if(!mode_features || (mode_features & ~HK_EXTERNAL_LINK_FEATURES_0_1))
        return HK_ERR_INVALID_ARGUMENT;
    if(!runtime->ops.external_link) return HK_ERR_CAPABILITY_ABSENT;
    if(runtime->external_link.service) {
        if((runtime->external_link.mode_features & mode_features) != mode_features)
            return HK_ERR_BUSY;
        *session = &runtime->external_link;
        return HK_OK;
    }
    if(runtime->teardown_started) return HK_ERR_INVALID_STATE;
    result = hk_external_link_open(runtime->ops.external_link, mode_features, &runtime->external_link);
    if(result == HK_OK) *session = &runtime->external_link;
    return result;
}
hk_result_t hk_app_context_display(
    const hk_app_context_t *ctx, uint32_t plane, hk_display_t **session)
{
    hk_app_runtime_t *runtime = NULL;
    hk_result_t result;
    if(!session)
        return HK_ERR_INVALID_ARGUMENT;
    *session = NULL;
    result = validate_callback_context(ctx, &runtime);
    if(result != HK_OK)
        return result;
    if(plane != HK_DISPLAY_PLANE_BASE && plane != HK_DISPLAY_PLANE_OVERLAY)
        return HK_ERR_INVALID_ARGUMENT;
    if(!runtime->ops.display)
        return HK_ERR_CAPABILITY_ABSENT;
    for(unsigned i = 0U; i < 2U; ++i)
        if(runtime->display[i].service && runtime->display[i].plane == plane) {
            *session = &runtime->display[i];
            return HK_OK;
        }
    if(runtime->teardown_started)
        return HK_ERR_INVALID_STATE;
    for(unsigned i = 0U; i < 2U; ++i)
        if(!runtime->display[i].service) {
            result = hk_display_open(runtime->ops.display, plane, &runtime->display[i]);
            if(result == HK_OK)
                *session = &runtime->display[i];
            return result;
        }
    return HK_ERR_BUSY;
}

hk_result_t hk_app_context_lights(
    const hk_app_context_t *ctx, uint32_t channels, hk_lights_t **session)
{
    hk_app_runtime_t *runtime = NULL;
    hk_result_t result;
    if(!session)
        return HK_ERR_INVALID_ARGUMENT;
    *session = NULL;
    result = validate_callback_context(ctx, &runtime);
    if(result != HK_OK)
        return result;
    if(!channels || (channels & ~HK_LIGHTS_CHANNEL_ALL))
        return HK_ERR_INVALID_ARGUMENT;
    if(!runtime->ops.lights)
        return HK_ERR_CAPABILITY_ABSENT;
    for(unsigned i = 0U; i < 3U; ++i)
        if(runtime->lights[i].service && runtime->lights[i].channels == channels) {
            *session = &runtime->lights[i];
            return HK_OK;
        }
    if(runtime->teardown_started)
        return HK_ERR_INVALID_STATE;
    for(unsigned i = 0U; i < 3U; ++i)
        if(!runtime->lights[i].service) {
            result = hk_lights_open(runtime->ops.lights, channels, &runtime->lights[i]);
            if(result == HK_OK)
                *session = &runtime->lights[i];
            return result;
        }
    return HK_ERR_BUSY;
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
    *state = s_callback_runtime->descriptor->entry->state_storage;
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
       s_callback_runtime->stage != HK_APP_STAGE_STOPPING)
        return HK_ERR_INVALID_STATE;
    *deadline = s_callback_runtime->teardown_deadline;
    return HK_OK;
}

hk_result_t hk_app_context_request_close(const hk_app_context_t *ctx)
{
    hk_app_runtime_t *runtime = NULL;
    hk_result_t result = validate_callback_context(ctx, &runtime);

    if(result != HK_OK)
        return result;
    if(runtime->state != HK_APP_RUNTIME_RUNNING)
        return HK_ERR_INVALID_STATE;
    runtime->close_requested = 1U;
    return HK_OK;
}

hk_result_t hk_app_context_deferred_token(
    const hk_app_context_t *ctx,
    hk_app_runtime_token_t *token)
{
    return hk_app_context_wakeup_token(ctx, 0U, token);
}

hk_result_t hk_app_context_wakeup_token(
    const hk_app_context_t *ctx,
    uint32_t value,
    hk_app_wakeup_token_t *token)
{
    hk_result_t result;

    if(!token)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_callback_context(ctx, NULL);
    if(result != HK_OK)
        return result;
    if(s_callback_runtime->state != HK_APP_RUNTIME_RUNNING)
        return HK_ERR_INVALID_STATE;
    token->struct_size = sizeof(*token);
    token->struct_version = HK_APP_WAKEUP_TOKEN_VERSION;
    token->slot = HK_APP_RUNTIME_SLOT;
    token->context_generation = ctx->generation;
    token->epoch = s_callback_runtime->active_epoch;
    token->value = value;
    return HK_OK;
}

hk_result_t hk_app_runtime_validate_wakeup_token(
    const hk_app_runtime_t *runtime,
    hk_app_wakeup_token_t token)
{
    if(!runtime || token.struct_size < sizeof(token) ||
       token.struct_version != HK_APP_WAKEUP_TOKEN_VERSION ||
       token.slot != HK_APP_RUNTIME_SLOT ||
       runtime->state != HK_APP_RUNTIME_RUNNING || !runtime->context_valid ||
       token.context_generation != runtime->context_generation ||
       token.epoch != runtime->active_epoch)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

hk_result_t hk_app_runtime_validate_token(
    const hk_app_runtime_t *runtime,
    hk_app_runtime_token_t token)
{
    return hk_app_runtime_validate_wakeup_token(runtime, token);
}

static uint8_t rect_is_valid(const hk_display_rect_t *region)
{
    return (uint8_t)(region && region->width > 0U && region->height > 0U);
}

hk_result_t hk_app_context_request_render(
    const hk_app_context_t *ctx,
    const hk_display_rect_t *region)
{
    hk_app_runtime_t *runtime = NULL;
    hk_result_t result = validate_callback_context(ctx, &runtime);

    if(result != HK_OK)
        return result;
    if(runtime->state != HK_APP_RUNTIME_RUNNING)
        return HK_ERR_INVALID_STATE;
    if(!runtime->descriptor->entry->render)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(!region)
    {
        runtime->full_invalidation = 1U;
        runtime->invalidation_count = 0U;
        return HK_OK;
    }
    if(!rect_is_valid(region))
        return HK_ERR_INVALID_ARGUMENT;
    if(runtime->full_invalidation)
        return HK_OK;
    if(runtime->invalidation_count >= HK_APP_MAX_INVALIDATIONS)
        return HK_ERR_LIMIT;
    runtime->invalidations[runtime->invalidation_count++] = *region;
    return HK_OK;
}

uint8_t hk_app_runtime_render_pending(const hk_app_runtime_t *runtime)
{
    return (uint8_t)(runtime && runtime->state == HK_APP_RUNTIME_RUNNING &&
                     (runtime->full_invalidation ||
                      runtime->invalidation_count > 0U));
}

uint16_t hk_app_runtime_invalidations(
    const hk_app_runtime_t *runtime,
    const hk_display_rect_t **regions,
    uint8_t *full)
{
    if(regions)
        *regions = runtime ? runtime->invalidations : NULL;
    if(full)
        *full = runtime ? runtime->full_invalidation : 0U;
    return runtime ? runtime->invalidation_count : 0U;
}

void hk_app_runtime_render_committed(hk_app_runtime_t *runtime)
{
    if(!runtime)
        return;
    runtime->full_invalidation = 0U;
    runtime->invalidation_count = 0U;
    memset(runtime->invalidations, 0, sizeof(runtime->invalidations));
}
