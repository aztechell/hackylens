#include "micropython_camera_codes.h"

#include "hk_config.h"
#if HK_ENABLE_QR_DECODER || HK_ENABLE_APRILTAG_DECODER
#include "../../services/camera_code_decoder.h"

static micropython_binding_result_t stage_frame(
    uint32_t lease, vision_source_t source, uint8_t pass, uint8_t *image,
    camera_code_frame_t *frame)
{
    const uint32_t args[6] = {lease, source, pass};
    size_t output_length = 0U;
    /* The synchronized request carries an uncached luma buffer address. */
    micropython_binding_result_t result = micropython_binding_call(
        MICROPYTHON_BINDING_OP_CAMERA_CODE_FRAME, args,
        (const uint8_t *)&image, sizeof(image),
        (uint8_t *)frame, sizeof(*frame), &output_length);
    if(result != MICROPYTHON_BINDING_OK)
        return result;
    return output_length == sizeof(*frame) ? MICROPYTHON_BINDING_OK : MICROPYTHON_BINDING_ERROR_IO;
}
#endif

micropython_binding_result_t micropython_camera_codes_scan(
    uint32_t lease, vision_source_t source, vision_result_snapshot_t *snapshot)
{
#if HK_ENABLE_QR_DECODER || HK_ENABLE_APRILTAG_DECODER
    const uint8_t count = camera_code_pass_count(source);
    if(!count)
        return MICROPYTHON_BINDING_ERROR_UNAVAILABLE;
    uint8_t *image;
    if(camera_code_prepare(source, &image) != HK_OK)
        return MICROPYTHON_BINDING_ERROR_IO;
    for(uint8_t pass = 0U; pass < count; pass++)
    {
        camera_code_frame_t frame;
        micropython_binding_result_t result = stage_frame(lease, source, pass, image, &frame);
        if(result != MICROPYTHON_BINDING_OK)
            return result;
        camera_code_decode(source, &frame, image, snapshot);
        if(snapshot->count)
            break;
    }
    return MICROPYTHON_BINDING_OK;
#else
    (void)lease;
    (void)source;
    (void)snapshot;
    return MICROPYTHON_BINDING_ERROR_UNAVAILABLE;
#endif
}

void micropython_camera_codes_cleanup(void)
{
#if HK_ENABLE_QR_DECODER || HK_ENABLE_APRILTAG_DECODER
    camera_code_close();
#endif
}
