#include "camera_code_decoder.h"

#include <string.h>

#include "hk_config.h"
#include "camera_client.h"
#include "vision_result_service.h"
#if HK_ENABLE_QR_DECODER
#include "qr_decoder_engine.h"
#include "qr_luma.h"
#include "../core/shared_buffer.h"

#define CODE_QR_WIDTH 320U
#define CODE_QR_HEIGHT 240U

static const uint8_t s_qr_passes[] = {0U, QR_PASS_INVERT, QR_PASS_LOCAL};
static qr_decoder_engine_t s_qr;
#endif
#if HK_ENABLE_APRILTAG_DECODER
#include "apriltag_decoder.h"
static apriltag_decoder_t s_tags;
#endif

uint8_t camera_code_pass_count(vision_source_t source)
{
#if HK_ENABLE_QR_DECODER
    if(source == VISION_SOURCE_QR)
        return sizeof(s_qr_passes);
#endif
#if HK_ENABLE_APRILTAG_DECODER
    if(source == VISION_SOURCE_APRILTAG)
        return 1U;
#endif
    (void)source;
    return 0U;
}

hk_result_t camera_code_stage(const void *owner, uint32_t lease, vision_source_t source,
                              uint8_t pass, uint8_t *image, camera_code_frame_t *metadata)
{
    uint8_t count = camera_code_pass_count(source);
    if(!count)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(!metadata || pass >= count)
        return HK_ERR_INVALID_ARGUMENT;
    camera_preview_frame_t frame;
    hk_result_t result = camera_client_get(owner, lease, &frame);
    if(result != HK_OK)
        return result;
#if HK_ENABLE_QR_DECODER
    if(source == VISION_SOURCE_QR)
    {
        if(!image || frame.width != CODE_QR_WIDTH || frame.height != CODE_QR_HEIGHT)
            return HK_ERR_INVALID_ARGUMENT;
        const uint8_t mode = s_qr_passes[pass];
        if(mode == QR_PASS_LOCAL)
            qr_luma_fill_local_qvga(frame.pixels, image, QR_LUMA_DVP_PAIRS);
        else
            qr_luma_fill_image(frame.pixels, image, frame.width, frame.height,
                               frame.width, 0U, 0U, frame.width, frame.height,
                               mode | QR_LUMA_DVP_PAIRS, 0U, NULL);
    }
#endif
#if HK_ENABLE_APRILTAG_DECODER
    if(source == VISION_SOURCE_APRILTAG)
        apriltag_decoder_fill(frame.pixels, frame.width, frame.height, NULL);
#endif
    (void)image;
    *metadata = (camera_code_frame_t){frame.sequence, frame.width, frame.height, frame.rotate};
    return HK_OK;
}

hk_result_t camera_code_prepare(vision_source_t source, uint8_t **image)
{
    *image = NULL;
#if HK_ENABLE_QR_DECODER
    if(source == VISION_SOURCE_QR)
    {
        *image = hk_shared_buffer_uncached(
            qr_decoder_engine_prepare(&s_qr, CODE_QR_WIDTH, CODE_QR_HEIGHT));
        return *image ? HK_OK : HK_ERR_IO;
    }
#endif
#if HK_ENABLE_APRILTAG_DECODER
    if(source == VISION_SOURCE_APRILTAG)
        return apriltag_decoder_open(&s_tags) ? HK_OK : HK_ERR_IO;
#endif
    (void)source;
    return HK_ERR_FEATURE_UNAVAILABLE;
}

void camera_code_decode(vision_source_t source, const camera_code_frame_t *frame,
                         uint8_t *image, vision_result_snapshot_t *snapshot)
{
    *snapshot = (vision_result_snapshot_t){
        .source = source, .frame_id = frame->sequence,
        .width = frame->rotate ? frame->height : frame->width,
        .height = frame->rotate ? frame->width : frame->height,
    };
#if HK_ENABLE_QR_DECODER
    if(source == VISION_SOURCE_QR)
    {
        qr_engine_scan_t scan;
        qr_decoder_engine_scan(&s_qr, image, &scan);
        if(scan.found)
        {
            snapshot->items[0] = vision_result_block(
                0U, 0U, 0U, scan.x0, scan.y0, scan.x1 + 1U, scan.y1 + 1U,
                frame->height, frame->rotate);
            snapshot->count = 1U;
            snapshot->qr_length = scan.payload_len;
            memcpy(snapshot->qr_payload, scan.payload, scan.payload_len);
        }
    }
#endif
#if HK_ENABLE_APRILTAG_DECODER
    if(source == VISION_SOURCE_APRILTAG)
    {
        apriltag_result_t tags[APRILTAG_RESULT_MAX];
        uint8_t count = apriltag_decoder_scan(&s_tags, frame->width, frame->height,
                                              APRILTAG_REFINE_EDGES, NULL, tags);
        for(uint8_t i = 0U; i < count && i < VISION_RESULT_MAX_ITEMS; i++)
        {
            const apriltag_result_t *tag = &tags[i];
            snapshot->items[snapshot->count++] = vision_result_block(
                tag->id, tag->hamming, tag->confidence,
                tag->x, tag->y, tag->x + tag->w, tag->y + tag->h,
                frame->height, frame->rotate);
        }
    }
#endif
    (void)image;
}

void camera_code_close(void)
{
#if HK_ENABLE_QR_DECODER
    qr_decoder_engine_close(&s_qr);
#endif
#if HK_ENABLE_APRILTAG_DECODER
    apriltag_decoder_close(&s_tags);
#endif
}
