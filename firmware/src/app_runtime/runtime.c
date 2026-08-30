#include "runtime_private.h"

#include <limits.h>
#include <string.h>

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
       !entry->probe || !entry->prepare || !entry->start || !entry->event ||
       !entry->tick || !entry->render || !entry->stop || !entry->cleanup)
        return HK_ERR_INVALID_ARGUMENT;
    state_address = (uintptr_t)entry->state_storage;
    if((state_address % descriptor->limits.state_alignment) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

static hk_result_t enter_callback(hk_app_runtime_t *runtime)
{
    if(runtime->callback_active)
        return HK_ERR_INVALID_STATE;
    runtime->callback_active = 1U;
    return HK_OK;
}

static hk_result_t finish_callback(
    hk_app_runtime_t *runtime,
    hk_result_t result)
{
    runtime->callback_active = 0U;
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
    const hk_app_v2_entry_t *entry = runtime->descriptor->entry.v2;
    uint8_t exhausted = 0U;

    runtime->stage = HK_APP_STAGE_INVALIDATING;
    runtime->context.valid = 0U;
    runtime->context.teardown_deadline_valid = 0U;
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
    runtime->prepare_entered = 0U;
    runtime->start_entered = 0U;
    runtime->stop_called = 0U;
    runtime->cleanup_called = 0U;
    runtime->teardown_started = 0U;
    runtime->callback_active = 0U;
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

    runtime->context.teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->context.teardown_deadline_valid = 1U;
    result = runtime->ops.deadline_after_us(
        runtime->ops.user,
        runtime->teardown_budget_us,
        &runtime->context.teardown_deadline);
    if(result == HK_OK &&
       runtime->context.teardown_deadline.at_us == UINT64_MAX)
        result = HK_ERR_INTERNAL;
    if(result != HK_OK)
    {
        runtime->context.teardown_deadline = HK_DEADLINE_IMMEDIATE;
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

    if(!hk_owner_is_zero(runtime->context.owner))
    {
        runtime->stage = HK_APP_STAGE_OWNER_CLEANUP;
        result = runtime->ops.owner_cleanup(
            runtime->ops.user,
            runtime->context.owner,
            runtime->context.teardown_deadline);
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
    if(!runtime || !ops || !ops->inject || !ops->owner_cleanup ||
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
    if(runtime->state != HK_APP_RUNTIME_INACTIVE || runtime->callback_active)
        return HK_ERR_BUSY;
    result = validate_descriptor(descriptor);
    if(result != HK_OK)
        return result;

    runtime->descriptor = descriptor;
    runtime->first_error = HK_OK;
    runtime->stop_reason = HK_APP_STOP_COMPLETED;
    runtime->context.runtime = runtime;
    runtime->context.owner = HK_OWNER_NONE;
    runtime->context.teardown_deadline = HK_DEADLINE_IMMEDIATE;
    runtime->context.generation = runtime->context_generation;
    runtime->context.epoch = runtime->active_epoch;
    runtime->context.valid = 1U;
    runtime->context.teardown_deadline_valid = 0U;
    entry = descriptor->entry.v2;
    memset(entry->state_storage, 0, descriptor->limits.state_bytes);

    runtime->stage = HK_APP_STAGE_PROBING;
    if(enter_callback(runtime) != HK_OK)
        return fail_without_teardown(runtime, HK_ERR_INVALID_STATE);
    result = finish_callback(runtime, entry->probe(&runtime->context));
    if(result != HK_OK)
        return fail_without_teardown(runtime, result);
    runtime->state = HK_APP_RUNTIME_PROBED;

    runtime->stage = HK_APP_STAGE_INJECTING;
    runtime->state = HK_APP_RUNTIME_INJECTING;
    result = runtime->ops.inject(
        runtime->ops.user, descriptor, &runtime->context, &owner);
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
    if(!ctx || !ctx->runtime || !ctx->valid ||
       ctx->generation != ctx->runtime->context_generation ||
       ctx != &ctx->runtime->context)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

hk_result_t hk_app_context_state(
    const hk_app_context_t *ctx,
    void **state,
    uint32_t *size_bytes)
{
    hk_result_t result;

    if(!state || !size_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_context(ctx);
    if(result != HK_OK)
        return result;
    if(!ctx->runtime->callback_active)
        return HK_ERR_WRONG_CONTEXT;
    *state = ctx->runtime->descriptor->entry.v2->state_storage;
    *size_bytes = ctx->runtime->descriptor->limits.state_bytes;
    return HK_OK;
}

hk_result_t hk_app_context_teardown_deadline(
    const hk_app_context_t *ctx,
    hk_deadline_t *deadline)
{
    hk_result_t result;

    if(!deadline)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_context(ctx);
    if(result != HK_OK)
        return result;
    if(!ctx->runtime->callback_active)
        return HK_ERR_WRONG_CONTEXT;
    if(!ctx->teardown_deadline_valid ||
       (ctx->runtime->stage != HK_APP_STAGE_STOPPING &&
        ctx->runtime->stage != HK_APP_STAGE_APP_CLEANUP))
        return HK_ERR_INVALID_STATE;
    *deadline = ctx->teardown_deadline;
    return HK_OK;
}

hk_result_t hk_app_context_deferred_token(
    const hk_app_context_t *ctx,
    hk_app_runtime_token_t *token)
{
    hk_result_t result;

    if(!token)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_context(ctx);
    if(result != HK_OK)
        return result;
    if(!ctx->runtime->callback_active)
        return HK_ERR_WRONG_CONTEXT;
    if(ctx->runtime->state != HK_APP_RUNTIME_RUNNING ||
       ctx->epoch != ctx->runtime->active_epoch)
        return HK_ERR_INVALID_STATE;
    token->slot = HK_APP_RUNTIME_SLOT;
    token->context_generation = ctx->generation;
    token->epoch = ctx->epoch;
    return HK_OK;
}

hk_result_t hk_app_runtime_validate_token(
    const hk_app_runtime_t *runtime,
    hk_app_runtime_token_t token)
{
    if(!runtime || token.slot != HK_APP_RUNTIME_SLOT ||
       runtime->state != HK_APP_RUNTIME_RUNNING || !runtime->context.valid ||
       token.context_generation != runtime->context_generation ||
       token.epoch != runtime->active_epoch)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}
