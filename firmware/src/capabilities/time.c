#include <hackylens/capability/time.h>

#include <limits.h>
#include <stddef.h>

#include "time_provider.h"

const hk_time_t *hk_time_service(void)
{
    return &hk_time_binding;
}

static hk_result_t time_now(const hk_time_t *time, uint64_t *value)
{
    hk_result_t result;

    if(!time)
        return HK_ERR_CAPABILITY_ABSENT;
    if(!time->now_us || !time->sleep_us || !time->fault ||
       time->max_sleep_us == 0U || time->max_sleep_us > HK_TIME_MAX_SLEEP_US ||
       time->max_slice_us == 0U || time->max_slice_us > HK_TIME_CANCEL_PROBE_MAX_US)
        return HK_ERR_INTERNAL;
    result = time->now_us(time->context, value);
    if(result == HK_ERR_INTERNAL)
        time->fault(time->context);
    return result;
}

static uint8_t cancelled(const hk_cancel_t *cancel)
{
    return (uint8_t)(cancel && cancel->probe &&
                     cancel->probe(cancel->context));
}

hk_result_t hk_time_now_us(
    const hk_time_t *handle,
    uint64_t *value)
{

    if(!value)
        return HK_ERR_INVALID_ARGUMENT;
    return time_now(handle, value);
}

hk_result_t hk_time_deadline_after_us(
    const hk_time_t *handle,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    uint64_t now;
    hk_result_t result;

    if(!deadline)
        return HK_ERR_INVALID_ARGUMENT;
    deadline->at_us = 0U;
    result = time_now(handle, &now);
    if(result != HK_OK)
        return result;
    if(duration_us > handle->max_sleep_us ||
       duration_us >= UINT64_MAX - now)
        return HK_ERR_LIMIT;
    deadline->at_us = now + duration_us;
    return HK_OK;
}

hk_result_t hk_time_sleep_until(
    const hk_time_t *handle,
    hk_deadline_t wake_target,
    hk_deadline_t operation_deadline,
    const hk_cancel_t *cancel)
{
    uint64_t now;
    hk_result_t result;

    if(wake_target.at_us == UINT64_MAX ||
       operation_deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = time_now(handle, &now);
    if(result != HK_OK)
        return result;
    if(now >= wake_target.at_us)
        return HK_OK;
    if(cancelled(cancel))
        return HK_ERR_CANCELLED;
    if(operation_deadline.at_us == 0U || now >= operation_deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(wake_target.at_us - now > handle->max_sleep_us ||
       operation_deadline.at_us - now > handle->max_sleep_us)
        return HK_ERR_LIMIT;

    while(1)
    {
        uint64_t stop_at = wake_target.at_us < operation_deadline.at_us ?
                           wake_target.at_us : operation_deadline.at_us;
        uint64_t slice_us = stop_at - now;
        uint64_t previous = now;

        if(slice_us > handle->max_slice_us)
            slice_us = handle->max_slice_us;
        result = handle->sleep_us(handle->context, slice_us);
        if(result != HK_OK)
        {
            if(result == HK_ERR_INTERNAL)
                handle->fault(handle->context);
            return result;
        }
        result = time_now(handle, &now);
        if(result != HK_OK)
            return result;
        if(now <= previous)
        {
            handle->fault(handle->context);
            return HK_ERR_INTERNAL;
        }
        if(now >= wake_target.at_us)
            return HK_OK;
        if(cancelled(cancel))
            return HK_ERR_CANCELLED;
        if(now >= operation_deadline.at_us)
            return HK_ERR_DEADLINE_EXCEEDED;
    }
}
