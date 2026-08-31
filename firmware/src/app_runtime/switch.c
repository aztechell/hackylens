#include "switch.h"

#include <limits.h>
#include <string.h>

static hk_app_stop_reason_t normalized_reason(hk_app_stop_reason_t reason)
{
    return (unsigned)reason <= (unsigned)HK_APP_STOP_SHUTDOWN
               ? reason
               : HK_APP_STOP_FORCED;
}

static uint8_t active_is_v2(const hk_app_switch_t *switcher)
{
    return (uint8_t)(switcher && switcher->active &&
                     switcher->active->lifecycle == HK_APP_LIFECYCLE_V2);
}

static void sync_v2_state(hk_app_switch_t *switcher)
{
    if(active_is_v2(switcher) &&
       hk_app_runtime_state(&switcher->runtime) != HK_APP_RUNTIME_RUNNING)
    {
        switcher->active = NULL;
        switcher->next_tick_us = 0U;
        hk_app_surface_private_invalidate(&switcher->surface);
    }
}

static hk_result_t close_active(
    hk_app_switch_t *switcher,
    hk_app_stop_reason_t reason)
{
    const hk_app_t *app = switcher->active;
    hk_result_t first = HK_OK;

    if(!app)
        return HK_OK;
    reason = normalized_reason(reason);
    if(app->lifecycle == HK_APP_LIFECYCLE_V2)
    {
        first = hk_app_runtime_stop(&switcher->runtime, reason);
    }
    else
    {
        const hk_legacy_app_entry_t *entry = hk_app_legacy_entry(app);

        if(entry && entry->exit)
            entry->exit();
        if(switcher->ops.legacy_close)
            first = switcher->ops.legacy_close(switcher->ops.user, app);
    }
    switcher->active = NULL;
    switcher->next_tick_us = 0U;
    hk_app_surface_private_invalidate(&switcher->surface);
    return first;
}

hk_result_t hk_app_switch_init(
    hk_app_switch_t *switcher,
    const hk_app_runtime_ops_t *runtime_ops,
    const hk_app_switch_ops_t *ops,
    uint64_t teardown_budget_us)
{
    hk_result_t result;

    if(!switcher || !runtime_ops || !ops || !ops->legacy_open ||
       !ops->legacy_close || !ops->now_us || !ops->render_begin ||
       !ops->render_present || !ops->render_abort)
        return HK_ERR_INVALID_ARGUMENT;
    memset(switcher, 0, sizeof(*switcher));
    switcher->runtime_ops = *runtime_ops;
    switcher->ops = *ops;
    result = hk_app_runtime_init(
        &switcher->runtime, &switcher->runtime_ops, teardown_budget_us);
    return result;
}

hk_result_t hk_app_switch_open(
    hk_app_switch_t *switcher,
    const hk_app_t *app,
    const hk_input_snapshot_t *legacy_input)
{
    hk_result_t result;
    uint64_t now_us = 0U;

    if(!switcher || !app)
        return HK_ERR_INVALID_ARGUMENT;
    if(switcher->transition_active)
        return HK_ERR_BUSY;
    switcher->transition_active = 1U;
    if(switcher->active)
    {
        result = close_active(switcher, HK_APP_STOP_SWITCH);
        if(result != HK_OK)
        {
            switcher->transition_active = 0U;
            return result;
        }
    }

    switcher->opening = 1U;
    switcher->pending_close = 0U;
    switcher->active = app;
    if(app->lifecycle == HK_APP_LIFECYCLE_V2)
    {
        result = hk_app_runtime_launch(&switcher->runtime, app);
        if(result == HK_OK)
        {
            result = switcher->ops.now_us(switcher->ops.user, &now_us);
            if(result == HK_OK &&
               now_us <= UINT64_MAX - app->limits.tick_interval_us)
                switcher->next_tick_us =
                    now_us + app->limits.tick_interval_us;
            else if(result == HK_OK)
                result = HK_ERR_LIMIT;
            if(result != HK_OK)
                (void)hk_app_runtime_stop(
                    &switcher->runtime, HK_APP_STOP_DEADLINE);
        }
    }
    else
    {
        const hk_legacy_app_entry_t *entry = hk_app_legacy_entry(app);

        if(!entry || !entry->enter)
            result = HK_ERR_INVALID_ARGUMENT;
        else
        {
            result = switcher->ops.legacy_open(switcher->ops.user, app);
            if(result == HK_OK)
                entry->enter(legacy_input);
        }
    }
    switcher->opening = 0U;
    switcher->transition_active = 0U;
    sync_v2_state(switcher);
    if(result != HK_OK)
    {
        switcher->active = NULL;
        return result;
    }
    if(switcher->pending_close)
    {
        hk_app_stop_reason_t reason = switcher->pending_close_reason;

        switcher->pending_close = 0U;
        result = close_active(switcher, reason);
        return result == HK_OK ? HK_ERR_CANCELLED : result;
    }
    return HK_OK;
}

hk_result_t hk_app_switch_close(
    hk_app_switch_t *switcher,
    hk_app_stop_reason_t reason)
{
    if(!switcher)
        return HK_ERR_INVALID_ARGUMENT;
    reason = normalized_reason(reason);
    if(switcher->transition_active)
    {
        if(!switcher->opening)
            return HK_ERR_INVALID_STATE;
        if(!switcher->pending_close)
        {
            switcher->pending_close = 1U;
            switcher->pending_close_reason = reason;
        }
        return HK_PENDING;
    }
    switcher->transition_active = 1U;
    {
        hk_result_t result = close_active(switcher, reason);
        switcher->transition_active = 0U;
        return result;
    }
}

static hk_result_t dispatch_event(
    hk_app_switch_t *switcher,
    hk_app_event_t *event)
{
    hk_result_t result;

    event->struct_size = sizeof(*event);
    event->struct_version = HK_APP_EVENT_VERSION;
    event->sequence = ++switcher->runtime.event_sequence;
    result = hk_app_runtime_event(&switcher->runtime, event);
    sync_v2_state(switcher);
    return result;
}

hk_result_t hk_app_switch_input(
    hk_app_switch_t *switcher,
    const hk_input_event_t *input,
    uint8_t *consumed)
{
    hk_app_event_t event = {0};

    if(!switcher || !input || !consumed)
        return HK_ERR_INVALID_ARGUMENT;
    *consumed = active_is_v2(switcher);
    if(!*consumed)
        return HK_OK;
    if(input->pressed & HK_INPUT_BUTTON_BACK)
        return hk_app_switch_close(switcher, HK_APP_STOP_BACK);
    event.kind = HK_APP_EVENT_INPUT;
    event.timestamp_us = input->timestamp_us;
    event.data.input = *input;
    return dispatch_event(switcher, &event);
}

hk_result_t hk_app_switch_media(
    hk_app_switch_t *switcher,
    hk_app_media_kind_t kind,
    uint32_t generation,
    uint64_t timestamp_us)
{
    hk_app_event_t event = {0};

    if(!switcher || kind < HK_APP_MEDIA_INSERTED ||
       kind > HK_APP_MEDIA_ERROR)
        return HK_ERR_INVALID_ARGUMENT;
    if(!active_is_v2(switcher))
        return HK_OK;
    event.kind = HK_APP_EVENT_MEDIA;
    event.timestamp_us = timestamp_us;
    event.data.media.kind = kind;
    event.data.media.generation = generation;
    return dispatch_event(switcher, &event);
}

hk_result_t hk_app_switch_wakeup(
    hk_app_switch_t *switcher,
    hk_app_wakeup_token_t token,
    uint64_t timestamp_us)
{
    hk_app_event_t event = {0};
    hk_result_t result;

    if(!switcher)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_runtime_validate_wakeup_token(&switcher->runtime, token);
    if(result != HK_OK)
        return result;
    event.kind = HK_APP_EVENT_WAKEUP;
    event.timestamp_us = timestamp_us;
    event.data.wakeup.token = token;
    return dispatch_event(switcher, &event);
}

static hk_result_t deadline_failure(
    hk_app_switch_t *switcher,
    hk_result_t result)
{
    hk_result_t stop_result =
        hk_app_switch_close(switcher, HK_APP_STOP_DEADLINE);
    return result != HK_OK ? result : stop_result;
}

static hk_result_t callback_failure(
    hk_app_switch_t *switcher,
    hk_result_t result)
{
    hk_result_t stop_result =
        hk_app_switch_close(switcher, HK_APP_STOP_CALLBACK_FAILED);
    return result != HK_OK ? result : stop_result;
}

static hk_result_t poll_tick(hk_app_switch_t *switcher, uint64_t now_us)
{
    hk_app_event_t event = {0};
    uint64_t started_us;
    uint64_t finished_us;
    hk_result_t result;

    if(now_us < switcher->next_tick_us)
        return HK_OK;
    result = switcher->ops.now_us(switcher->ops.user, &started_us);
    if(result != HK_OK)
        return deadline_failure(switcher, result);
    event.kind = HK_APP_EVENT_TIMER;
    event.timestamp_us = started_us;
    event.data.timer.scheduled_us = switcher->next_tick_us;
    event.data.timer.now_us = started_us;
    result = dispatch_event(switcher, &event);
    if(result != HK_OK || !active_is_v2(switcher))
        return result;
    result = hk_app_runtime_tick(&switcher->runtime, started_us);
    sync_v2_state(switcher);
    if(result != HK_OK || !active_is_v2(switcher))
        return result;
    result = switcher->ops.now_us(switcher->ops.user, &finished_us);
    if(result != HK_OK)
        return deadline_failure(switcher, result);
    if(finished_us < started_us ||
       finished_us - started_us > switcher->active->limits.tick_budget_us)
        return deadline_failure(switcher, HK_ERR_DEADLINE_EXCEEDED);
    if(finished_us > UINT64_MAX - switcher->active->limits.tick_interval_us)
        return deadline_failure(switcher, HK_ERR_LIMIT);
    switcher->next_tick_us =
        finished_us + switcher->active->limits.tick_interval_us;
    return HK_OK;
}

static hk_result_t poll_render(hk_app_switch_t *switcher)
{
    const hk_display_rect_t *regions = NULL;
    uint8_t full = 0U;
    uint16_t count;
    uint64_t started_us;
    uint64_t finished_us;
    hk_deadline_t deadline = HK_DEADLINE_IMMEDIATE;
    hk_result_t result;

    if(!hk_app_runtime_render_pending(&switcher->runtime))
        return HK_OK;
    result = switcher->ops.render_begin(
        switcher->ops.user, &switcher->runtime, &switcher->surface);
    if(result == HK_ERR_CAPABILITY_ABSENT)
    {
        hk_app_runtime_render_committed(&switcher->runtime);
        return HK_OK;
    }
    if(result != HK_OK)
        return callback_failure(switcher, result);
    count = hk_app_runtime_invalidations(
        &switcher->runtime, &regions, &full);
    if(full)
        result = hk_app_surface_invalidate(&switcher->surface, NULL);
    for(uint16_t index = 0U; result == HK_OK && index < count; index++)
        result = hk_app_surface_invalidate(
            &switcher->surface, &regions[index]);
    if(result != HK_OK)
    {
        (void)switcher->ops.render_abort(switcher->ops.user);
        hk_app_surface_private_invalidate(&switcher->surface);
        return callback_failure(switcher, result);
    }
    result = switcher->runtime_ops.deadline_after_us(
        switcher->runtime_ops.user,
        switcher->active->limits.render_budget_us,
        &deadline);
    if(result == HK_OK && deadline.at_us == UINT64_MAX)
        result = HK_ERR_INTERNAL;
    if(result == HK_OK)
        result = switcher->ops.now_us(switcher->ops.user, &started_us);
    if(result != HK_OK)
    {
        (void)switcher->ops.render_abort(switcher->ops.user);
        hk_app_surface_private_invalidate(&switcher->surface);
        return deadline_failure(switcher, result);
    }
    hk_app_runtime_render_committed(&switcher->runtime);
    result = hk_app_runtime_render(
        &switcher->runtime, &switcher->surface);
    sync_v2_state(switcher);
    if(result != HK_OK || !active_is_v2(switcher))
    {
        (void)switcher->ops.render_abort(switcher->ops.user);
        hk_app_surface_private_invalidate(&switcher->surface);
        return result;
    }
    result = switcher->ops.now_us(switcher->ops.user, &finished_us);
    if(result != HK_OK || finished_us < started_us ||
       finished_us - started_us > switcher->active->limits.render_budget_us)
    {
        (void)switcher->ops.render_abort(switcher->ops.user);
        hk_app_surface_private_invalidate(&switcher->surface);
        return deadline_failure(
            switcher,
            result != HK_OK ? result : HK_ERR_DEADLINE_EXCEEDED);
    }
    result = switcher->surface.invalidated
                 ? switcher->ops.render_present(
                       switcher->ops.user, deadline)
                 : switcher->ops.render_abort(switcher->ops.user);
    hk_app_surface_private_invalidate(&switcher->surface);
    if(result != HK_OK)
    {
        (void)switcher->ops.render_abort(switcher->ops.user);
        return callback_failure(switcher, result);
    }
    return HK_OK;
}

hk_result_t hk_app_switch_poll(
    hk_app_switch_t *switcher,
    uint64_t now_us)
{
    hk_result_t result;

    if(!switcher)
        return HK_ERR_INVALID_ARGUMENT;
    if(!active_is_v2(switcher))
        return HK_OK;
    result = poll_tick(switcher, now_us);
    if(result != HK_OK || !active_is_v2(switcher))
        return result;
    return poll_render(switcher);
}

const hk_app_t *hk_app_switch_active(const hk_app_switch_t *switcher)
{
    return switcher ? switcher->active : NULL;
}

uint32_t hk_app_switch_poll_interval_us(
    const hk_app_switch_t *switcher,
    uint64_t now_us)
{
    uint64_t remaining;

    if(!active_is_v2(switcher))
        return 1U;
    if(hk_app_runtime_render_pending(&switcher->runtime) ||
       switcher->next_tick_us <= now_us)
        return 1U;
    remaining = switcher->next_tick_us - now_us;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}
