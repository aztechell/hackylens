#include <hackylens/capability/display.h>
#include <limits.h>
#include <stddef.h>
#include "display_provider.h"
#include "display_stage_private.h"

const hk_display_service_t *hk_display_service(void)
{
    return hk_display_binding.get_info ? &hk_display_binding : NULL;
}

static int plane_index(uint32_t plane)
{
    return plane == HK_DISPLAY_PLANE_BASE ? 0 :
        plane == HK_DISPLAY_PLANE_OVERLAY ? 1 : -1;
}

static hk_result_t display_provider_for(
    const hk_display_t *handle, const hk_display_service_t **provider)
{
    int index;
    if(!handle || !provider)
        return HK_ERR_INVALID_ARGUMENT;
    index = plane_index(handle->plane);
    if(!handle->service || !handle->service->state || index < 0 ||
       handle->service->state->claimants[index] != handle)
        return HK_ERR_STALE_HANDLE;
    *provider = handle->service;
    if((*provider)->state->quarantined & handle->plane)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t quarantine_internal(
    const hk_display_t *handle, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL && handle && handle->service &&
       handle->service->state && plane_index(handle->plane) >= 0 &&
       handle->service->state->claimants[plane_index(handle->plane)] == handle)
        handle->service->state->quarantined |= handle->plane;
    return result;
}

hk_result_t hk_display_open(
    const hk_display_service_t *service, uint32_t plane, hk_display_t *handle)
{
    hk_result_t result;
    int index = plane_index(plane);
    if(!handle || index < 0)
        return HK_ERR_INVALID_ARGUMENT;
    if(!service)
        return HK_ERR_CAPABILITY_ABSENT;
    if(!service->state || !service->open_plane || !service->close_plane ||
       !service->retire_plane || !service->get_info || !service->begin_batch ||
       !service->set_clip || !service->clear || !service->fill_rect ||
       !service->stroke_rect || !service->text || !service->blit ||
       !service->mark_dirty || !service->surface_acquire || !service->present ||
       !service->abort || !service->stage_checkpoint || !service->stage_restore ||
       !service->stage_keep_last_clear || service->reserved)
        return HK_ERR_INTERNAL;
    if(service->state->claimants[0] == handle ||
       service->state->claimants[1] == handle ||
       service->state->claimants[index])
        return HK_ERR_BUSY;
    if(service->state->quarantined & plane)
        return HK_ERR_INTERNAL;
    handle->service = service;
    handle->plane = plane;
    result = service->open_plane(service->context, handle, plane);
    if(result != HK_OK)
    {
        handle->service = NULL;
        handle->plane = 0U;
        return result;
    }
    service->state->claimants[index] = handle;
    return HK_OK;
}

hk_result_t hk_display_close(hk_display_t *handle, hk_deadline_t deadline)
{
    const hk_display_service_t *service;
    hk_result_t result;
    if(!handle || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(!handle->service && !handle->plane)
        return HK_OK;
    result = display_provider_for(handle, &service);
    if(result != HK_OK)
        return result;
    result = service->close_plane(service->context, handle, deadline);
    if(result != HK_OK)
        return quarantine_internal(handle, result);
    service->state->claimants[plane_index(handle->plane)] = NULL;
    handle->service = NULL;
    handle->plane = 0U;
    return HK_OK;
}

hk_result_t hk_display_retire(hk_display_t *handle, hk_deadline_t deadline)
{
    const hk_display_service_t *service;
    hk_result_t result;
    int index;
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    if(!handle->service && !handle->plane)
        return deadline.at_us == UINT64_MAX ? HK_ERR_INVALID_ARGUMENT : HK_OK;
    service = handle->service;
    index = plane_index(handle->plane);
    if(!service || !service->state || index < 0 ||
       service->state->claimants[index] != handle)
    {
        handle->service = NULL;
        handle->plane = 0U;
        return HK_ERR_STALE_HANDLE;
    }
    result = deadline.at_us == UINT64_MAX ? HK_ERR_INVALID_ARGUMENT :
        service->close_plane(service->context, handle, deadline);
    /* Invalidation is unconditional, including expired deadlines and partial
     * physical transfers. No borrowed caller memory may survive retirement. */
    service->retire_plane(service->context, handle);
    if(result != HK_OK)
        service->state->quarantined |= handle->plane;
    service->state->claimants[index] = NULL;
    handle->service = NULL;
    handle->plane = 0U;
    return result;
}

hk_result_t hk_display_get_info(
    const hk_display_t *handle, hk_display_info_t *info)
{
    const hk_display_service_t *provider;
    hk_result_t result;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->get_info(provider->context, info);
    return quarantine_internal(handle, result);
}

#define DISPLAY_CALL0(name)                                                 \
    hk_result_t hk_display_##name(                                          \
        const hk_display_t *handle)                                         \
    {                                                                       \
        const hk_display_service_t *provider;                               \
        hk_result_t result = display_provider_for(handle, &provider);       \
        if(result == HK_OK)                                                 \
            result = provider->name(provider->context, handle);             \
        return quarantine_internal(handle, result);                         \
    }

DISPLAY_CALL0(begin_batch)
DISPLAY_CALL0(abort)

hk_result_t hk_display_set_clip(
    const hk_display_t *handle,
    const hk_display_rect_t *clip)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->set_clip(provider->context, handle, clip);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_clear(
    const hk_display_t *handle, uint16_t color)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->clear(provider->context, handle, color);
    return quarantine_internal(handle, result);
}

#define DISPLAY_RECT_CALL(name)                                             \
    hk_result_t hk_display_##name(                                          \
        const hk_display_t *handle,                                         \
        const hk_display_rect_t *rect, uint16_t color)                      \
    {                                                                       \
        const hk_display_service_t *provider;                               \
        hk_result_t result = display_provider_for(handle, &provider);       \
        if(result == HK_OK)                                                 \
            result = provider->name(                                        \
                provider->context, handle, rect, color);                    \
        return quarantine_internal(handle, result);                         \
    }

DISPLAY_RECT_CALL(fill_rect)
DISPLAY_RECT_CALL(stroke_rect)

hk_result_t hk_display_text(
    const hk_display_t *handle,
    const hk_display_rect_t *bounds, const char *utf8,
    uint32_t size_bytes, uint16_t color)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->text(
            provider->context, handle, bounds,
            utf8, size_bytes, color);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_blit(
    const hk_display_t *handle,
    const hk_display_rect_t *destination, const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->blit(
            provider->context, handle, destination,
            pixels, pixel_format);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_mark_dirty(
    const hk_display_t *handle,
    const hk_display_rect_t *rect)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->mark_dirty(
            provider->context, handle, rect);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_surface_acquire(
    const hk_display_t *handle,
    hk_display_surface_t *surface)
{
    const hk_display_service_t *provider;
    hk_result_t result;
    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->surface_acquire(
            provider->context, handle, surface);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_present(
    const hk_display_t *handle,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    const hk_display_service_t *provider;
    hk_result_t result;
    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->present(
            provider->context, handle, deadline, cancel);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_stage_checkpoint(
    const hk_display_t *handle,
    uint16_t *commands, uint16_t *text_bytes)
{
    const hk_display_service_t *provider;
    hk_result_t result;
    if(!commands || !text_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->stage_checkpoint(
            provider->context, handle, commands, text_bytes);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_stage_restore(
    const hk_display_t *handle,
    uint16_t commands, uint16_t text_bytes)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->stage_restore(
            provider->context, handle, commands, text_bytes);
    return quarantine_internal(handle, result);
}

hk_result_t hk_display_stage_keep_last_clear(
    const hk_display_t *handle)
{
    const hk_display_service_t *provider;
    hk_result_t result = display_provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->stage_keep_last_clear(
            provider->context, handle);
    return quarantine_internal(handle, result);
}
