#include "camera_client.h"

#include <stddef.h>

#include "camera_session.h"

static const void *s_owner;
static camera_preview_frame_t s_frame;
static hk_color_tracker_t s_tracker;

hk_result_t camera_client_open(const void *owner)
{
    if(!owner)
        return HK_ERR_INVALID_ARGUMENT;
    if(s_owner)
        return s_owner == owner ? HK_OK : HK_ERR_BUSY;
    hk_result_t result = camera_service_client_start();
    if(result == HK_OK)
        s_owner = owner;
    return result;
}

uint8_t camera_client_is_open(const void *owner)
{
    return owner && s_owner == owner;
}

void camera_client_close(const void *owner)
{
    if(!owner || s_owner != owner)
        return;
    camera_service_preview_release(&s_frame);
    s_frame = (camera_preview_frame_t){0};
    camera_service_client_stop();
    s_owner = NULL;
}

void camera_client_tick(const void *owner)
{
    if(owner && owner == s_owner)
        camera_service_client_tick();
}

hk_result_t camera_client_acquire(const void *owner, camera_preview_frame_t *frame)
{
    if(!owner || !frame)
        return HK_ERR_INVALID_ARGUMENT;
    if(s_owner != owner)
        return HK_ERR_INVALID_STATE;
    if(s_frame.lease_id)
        return HK_ERR_BUSY;
    if(!camera_service_capture_ready() || camera_service_consume_frame_timeout())
        return HK_ERR_IO;
    if(!camera_service_preview_acquire(&s_frame))
        return HK_PENDING;
    if(!s_frame.lease_id)
        return HK_ERR_IO;
    *frame = s_frame;
    return HK_OK;
}

hk_result_t camera_client_get(const void *owner, uint32_t lease_id,
                              camera_preview_frame_t *frame)
{
    if(!frame)
        return HK_ERR_INVALID_ARGUMENT;
    if(!owner || owner != s_owner || !lease_id || lease_id != s_frame.lease_id)
        return HK_ERR_STALE_HANDLE;
    *frame = s_frame;
    return HK_OK;
}

hk_result_t camera_client_release(const void *owner, uint32_t lease_id)
{
    if(!owner || owner != s_owner || !lease_id || lease_id != s_frame.lease_id)
        return HK_ERR_STALE_HANDLE;
    camera_service_preview_release(&s_frame);
    s_frame = (camera_preview_frame_t){0};
    return HK_OK;
}

hk_result_t camera_client_find_color(
    const void *owner, uint32_t lease_id, const hk_color_rect_t *roi,
    const hk_color_threshold_t *threshold, uint32_t min_pixels, hk_color_blob_t *blob)
{
    camera_preview_frame_t frame;
    hk_result_t result = camera_client_get(owner, lease_id, &frame);
    if(result != HK_OK)
        return result;
    const hk_color_frame_t image = {frame.pixels, frame.width, frame.height, frame.rotate};
    return hk_color_find_largest(&s_tracker, &image, roi, threshold, min_pixels, blob) ?
        HK_OK : HK_ERR_INVALID_ARGUMENT;
}
