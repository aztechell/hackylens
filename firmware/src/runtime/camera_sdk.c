#include "hk_config.h"

#if HK_ENABLE_CAMERA_FEATURE
#include <stddef.h>
#include <hackylens/app/camera.h>

#include "app_services.h"
#include "data_sdk_private.h"
#include "../controllers/camera_client_preview.h"
#include "../services/camera_client.h"
#include "../services/camera_code_client.h"
#include "../services/camera_data_save.h"
#include "../services/camera_vision_client.h"

static uint8_t s_owner;
static uint32_t s_detect_lease;

void app_camera_retire(void)
{
    camera_code_client_close(&s_owner);
    camera_data_save_cancel(&s_owner);
    camera_vision_close(&s_owner);
    s_detect_lease = 0U;
    camera_client_close(&s_owner);
}

hk_result_t hk_app_camera_open(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? camera_client_open(&s_owner) : result;
}

hk_result_t hk_app_camera_close(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 0U);
    if(result == HK_OK)
        app_camera_retire();
    return result;
}

hk_result_t hk_app_camera_acquire(const hk_app_context_t *ctx, hk_app_camera_frame_t *frame)
{
    if(!frame)
        return HK_ERR_INVALID_ARGUMENT;
    hk_result_t result = app_service_context(ctx, 1U);
    if(result != HK_OK)
        return result;
    camera_client_tick(&s_owner);
    camera_preview_frame_t capture;
    result = camera_client_acquire(&s_owner, &capture);
    if(result == HK_OK)
        *frame = (hk_app_camera_frame_t){
            .lease = capture.lease_id, .sequence = capture.sequence,
            .width = capture.rotate ? capture.height : capture.width,
            .height = capture.rotate ? capture.width : capture.height,
        };
    return result;
}

hk_result_t hk_app_camera_release(const hk_app_context_t *ctx, uint32_t lease)
{
    hk_result_t result = app_service_context(ctx, 0U);
    if(result != HK_OK)
        return result;
    if((lease && lease == s_detect_lease) || camera_data_save_holds(&s_owner, lease) ||
       camera_code_client_holds(&s_owner, lease))
        return HK_ERR_BUSY;
    return camera_client_release(&s_owner, lease);
}

hk_result_t hk_app_camera_find_color(const hk_app_context_t *ctx, uint32_t lease,
                                     const hk_color_rect_t *roi,
                                     const hk_color_threshold_t *threshold,
                                     uint32_t min_pixels, hk_color_blob_t *blob)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ?
        camera_client_find_color(&s_owner, lease, roi, threshold, min_pixels, blob) : result;
}

hk_result_t hk_app_camera_present(const hk_app_context_t *ctx, uint32_t lease,
                                  const hk_color_rect_t *box, uint16_t color)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? camera_client_present(&s_owner, lease, box, color) : result;
}

hk_result_t hk_app_camera_save_begin(const hk_app_context_t *ctx, uint32_t lease,
                                     const char *name, size_t length)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ?
        app_data_result(camera_data_save_begin(&s_owner, lease, name, length)) : result;
}

hk_result_t hk_app_camera_save_step(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? app_data_result(camera_data_save_step(&s_owner)) : result;
}

hk_result_t hk_app_camera_save_cancel(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 0U);
    if(result == HK_OK)
        camera_data_save_cancel(&s_owner);
    return result;
}

hk_result_t hk_app_camera_scan_begin(const hk_app_context_t *ctx, uint32_t lease,
                                     vision_source_t source)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? camera_code_client_begin(&s_owner, lease, source) : result;
}

hk_result_t hk_app_camera_scan_poll(const hk_app_context_t *ctx,
                                    vision_result_snapshot_t *snapshot)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? camera_code_client_poll(&s_owner, snapshot) : result;
}

hk_result_t hk_app_camera_scan_cancel(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 0U);
    if(result == HK_OK)
        camera_code_client_close(&s_owner);
    return result;
}

hk_result_t hk_app_camera_model_open_step(const hk_app_context_t *ctx,
                                          vision_source_t source, const char *package)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ?
        camera_vision_open_step(&s_owner, source, package ? package : "") : result;
}

hk_result_t hk_app_camera_model_close(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 0U);
    if(result == HK_OK)
    {
        camera_vision_close(&s_owner);
        s_detect_lease = 0U;
    }
    return result;
}

hk_result_t hk_app_camera_detect_begin(const hk_app_context_t *ctx, uint32_t lease)
{
    hk_result_t result = app_service_context(ctx, 1U);
    if(result != HK_OK)
        return result;
    result = camera_vision_submit(&s_owner, lease);
    if(result == HK_OK)
        s_detect_lease = lease;
    return result;
}

hk_result_t hk_app_camera_detect_poll(const hk_app_context_t *ctx,
                                     vision_result_snapshot_t *snapshot)
{
    if(!snapshot)
        return HK_ERR_INVALID_ARGUMENT;
    hk_result_t result = app_service_context(ctx, 1U);
    if(result != HK_OK)
        return result;
    result = camera_vision_poll(&s_owner, snapshot);
    if(result != HK_PENDING)
        s_detect_lease = 0U;
    return result;
}
#endif
