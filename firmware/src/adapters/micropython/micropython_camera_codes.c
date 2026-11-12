#include "micropython_camera_codes.h"

#include <string.h>
#include "hk_config.h"

#if HK_ENABLE_APP_QR_CAMERA || HK_ENABLE_APP_APRILTAG
#include "../../services/camera_client.h"
#include "../../services/vision_result_service.h"
#endif
#if HK_ENABLE_APP_QR_CAMERA
#include "../../core/shared_buffer.h"
#include "../../services/qr_decoder_engine.h"
#include "../../services/qr_luma.h"

#define PYTHON_QR_WIDTH 320U
#define PYTHON_QR_HEIGHT 240U

static qr_decoder_engine_t s_qr;
#endif
#if HK_ENABLE_APP_APRILTAG
#include "../../services/apriltag_decoder.h"
static apriltag_decoder_t s_apriltag;
#endif

hk_result_t micropython_camera_codes_stage(const void *owner, uint32_t lease,
                                           vision_source_t source, uint8_t mode,
                                           uint8_t *qr_image,
                                           micropython_code_frame_t *metadata)
{
    (void)qr_image;
#if HK_ENABLE_APP_QR_CAMERA || HK_ENABLE_APP_APRILTAG
    camera_preview_frame_t frame;
    hk_result_t result = camera_client_get(owner, lease, &frame);
    if(result != HK_OK)
        return result;
    switch(source)
    {
#if HK_ENABLE_APP_QR_CAMERA
    case VISION_SOURCE_QR:
    {
        if(!qr_image || frame.width != PYTHON_QR_WIDTH || frame.height != PYTHON_QR_HEIGHT)
            return HK_ERR_INVALID_ARGUMENT;
        if(mode == QR_PASS_LOCAL)
            qr_luma_fill_local_qvga(frame.pixels, qr_image);
        else
            qr_luma_fill_image(frame.pixels, qr_image, frame.width, frame.height,
                               frame.width, 0U, 0U, frame.width, frame.height,
                               mode, 0U, NULL);
        break;
    }
#endif
#if HK_ENABLE_APP_APRILTAG
    case VISION_SOURCE_APRILTAG:
        apriltag_decoder_fill(frame.pixels, frame.width, frame.height);
        break;
#endif
    default:
        return HK_ERR_FEATURE_UNAVAILABLE;
    }
    *metadata = (micropython_code_frame_t){frame.sequence, frame.width, frame.height, frame.rotate};
    (void)mode;
    return HK_OK;
#else
    (void)owner; (void)lease; (void)source; (void)mode; (void)metadata;
    return HK_ERR_FEATURE_UNAVAILABLE;
#endif
}

#if HK_ENABLE_APP_QR_CAMERA || HK_ENABLE_APP_APRILTAG
static micropython_binding_result_t stage_frame(
    uint32_t lease, vision_source_t source, uint8_t mode, uint8_t *qr_image,
    micropython_code_frame_t *frame, vision_result_snapshot_t *snapshot)
{
    const uint32_t args[6] = {lease, source, mode};
    size_t output_length = 0U;
    /* Publish the buffer address in the synchronized request with the lease. */
    micropython_binding_result_t result = micropython_binding_call(
        MICROPYTHON_BINDING_OP_CAMERA_CODE_FRAME, args,
        (const uint8_t *)&qr_image, sizeof(qr_image),
        (uint8_t *)frame, sizeof(*frame), &output_length);
    if(result != MICROPYTHON_BINDING_OK)
        return result;
    if(output_length != sizeof(*frame))
        return MICROPYTHON_BINDING_ERROR_IO;
    *snapshot = (vision_result_snapshot_t){
        .source = source,
        .frame_id = frame->sequence,
        .width = frame->rotate ? frame->height : frame->width,
        .height = frame->rotate ? frame->width : frame->height,
    };
    return MICROPYTHON_BINDING_OK;
}
#endif

micropython_binding_result_t micropython_camera_codes_scan(
    uint32_t lease, vision_source_t source, vision_result_snapshot_t *snapshot)
{
#if HK_ENABLE_APP_QR_CAMERA
    if(source == VISION_SOURCE_QR)
    {
        static const uint8_t passes[] = {0U, QR_PASS_INVERT, QR_PASS_LOCAL};
        uint8_t *image = qr_decoder_engine_prepare(&s_qr, PYTHON_QR_WIDTH, PYTHON_QR_HEIGHT);
        if(!image)
            return MICROPYTHON_BINDING_ERROR_IO;
        image = hk_shared_buffer_uncached(image);
        qr_engine_scan_t scan;
        micropython_code_frame_t frame;
        for(size_t pass = 0U; pass < sizeof(passes); pass++)
        {
            micropython_binding_result_t result =
                stage_frame(lease, source, passes[pass], image, &frame, snapshot);
            if(result != MICROPYTHON_BINDING_OK)
                return result;
            qr_decoder_engine_scan(&s_qr, image, &scan);
            if(scan.found)
            {
                snapshot->items[0] = vision_result_block(
                    0U, 0U, 0U, scan.x0, scan.y0, scan.x1 + 1U, scan.y1 + 1U,
                    frame.height, frame.rotate);
                snapshot->count = 1U;
                snapshot->qr_length = scan.payload_len;
                memcpy(snapshot->qr_payload, scan.payload, scan.payload_len);
                break;
            }
        }
        return MICROPYTHON_BINDING_OK;
    }
#endif
#if HK_ENABLE_APP_APRILTAG
    if(source == VISION_SOURCE_APRILTAG)
    {
        micropython_code_frame_t frame;
        micropython_binding_result_t result =
            stage_frame(lease, source, 0U, NULL, &frame, snapshot);
        if(result != MICROPYTHON_BINDING_OK)
            return result;
        if(!apriltag_decoder_open(&s_apriltag))
            return MICROPYTHON_BINDING_ERROR_IO;
        apriltag_result_t tags[APRILTAG_RESULT_MAX];
        uint8_t count = apriltag_decoder_scan(&s_apriltag, frame.width, frame.height,
                                               APRILTAG_REFINE_EDGES, tags);
        for(uint8_t i = 0U; i < count && i < VISION_RESULT_MAX_ITEMS; i++)
        {
            const apriltag_result_t *tag = &tags[i];
            snapshot->items[snapshot->count++] = vision_result_block(
                tag->id, tag->hamming, tag->confidence,
                tag->x, tag->y, tag->x + tag->w, tag->y + tag->h,
                frame.height, frame.rotate);
        }
        return MICROPYTHON_BINDING_OK;
    }
#endif
    (void)lease; (void)source; (void)snapshot;
    return MICROPYTHON_BINDING_ERROR_UNAVAILABLE;
}

void micropython_camera_codes_cleanup(void)
{
#if HK_ENABLE_APP_QR_CAMERA
    qr_decoder_engine_close(&s_qr);
#endif
#if HK_ENABLE_APP_APRILTAG
    apriltag_decoder_close(&s_apriltag);
#endif
}
