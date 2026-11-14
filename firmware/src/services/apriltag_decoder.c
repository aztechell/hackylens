#include "apriltag_decoder.h"

#include <stddef.h>
#include <hackylens/capability/time.h>

#include "../core/camera_pixel_order.h"
#include "../core/shared_buffer.h"
#include "apriltag.h"
#include "tag36h11.h"

#define APRILTAG_QUAD_DECIMATE 2.0f
#define APRILTAG_MIN_CLUSTER_PIXELS 24

static uint8_t g_luma[APRILTAG_INPUT_W * APRILTAG_INPUT_H] __attribute__((aligned(64)));

static uint64_t apriltag_time_now_us(void)
{
    uint64_t now = 0U;
    (void)hk_time_now_us(hk_time_service(), &now);
    return now;
}

static uint8_t *shared_luma(void)
{
    return hk_shared_buffer_uncached(g_luma);
}

static uint8_t rgb565_luma(uint16_t color)
{
    uint16_t r = (uint16_t)(((color >> 11U) & 0x1fU) * 255U / 31U);
    uint16_t g = (uint16_t)(((color >> 5U) & 0x3fU) * 255U / 63U);
    uint16_t b = (uint16_t)((color & 0x1fU) * 255U / 31U);
    return (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8U);
}

static int16_t scaled_coordinate(double value, uint16_t source_limit, uint16_t output_limit)
{
    int32_t coordinate = (int32_t)(value * output_limit / source_limit + 0.5);
    if(coordinate < 0)
        coordinate = 0;
    if(coordinate >= output_limit)
        coordinate = output_limit ? output_limit - 1 : 0;
    return (int16_t)coordinate;
}

void apriltag_decoder_fill(const volatile uint16_t *pixels, uint16_t width, uint16_t height,
                           const apriltag_scan_region_t *region)
{
    uint8_t *luma = shared_luma();
    const uint32_t pair_xor = camera_dvp_pair_xor(width);
    if(region)
    {
        for(uint16_t y = 0; y < region->height; y++)
        {
            const volatile uint16_t *row = pixels + (uint32_t)(region->y + y) * width;
            uint8_t *output = luma + (uint32_t)y * APRILTAG_INPUT_W;
            for(uint16_t x = 0; x < region->width; x++)
                output[x] = rgb565_luma(row[(region->x + x) ^ pair_xor]);
        }
        return;
    }
    for(uint16_t y = 0; y < APRILTAG_INPUT_H; y++)
    {
        uint32_t source_y = (uint32_t)y * height / APRILTAG_INPUT_H;
        const volatile uint16_t *row = pixels + source_y * width;
        uint8_t *output = luma + (uint32_t)y * APRILTAG_INPUT_W;

        for(uint16_t x = 0; x < APRILTAG_INPUT_W; x++)
            output[x] = rgb565_luma(row[((uint32_t)x * width / APRILTAG_INPUT_W) ^ pair_xor]);
    }
}

uint8_t apriltag_decoder_open(apriltag_decoder_t *decoder)
{
    if(decoder->detector)
        return 1U;
    decoder->family = tag36h11_create();
    decoder->detector = apriltag_detector_create();
    if(!decoder->family || !decoder->detector)
    {
        apriltag_decoder_close(decoder);
        return 0U;
    }

    apriltag_detector_add_family_bits(decoder->detector, decoder->family, APRILTAG_MAX_HAMMING);
    decoder->detector->nthreads = 1;
    decoder->detector->quad_decimate = APRILTAG_QUAD_DECIMATE;
    decoder->detector->quad_sigma = 0.0f;
    decoder->detector->refine_edges = APRILTAG_REFINE_EDGES ? true : false;
    decoder->detector->decode_sharpening = 0.25;
    decoder->detector->debug = false;
    decoder->detector->qtp.min_cluster_pixels = APRILTAG_MIN_CLUSTER_PIXELS;
    decoder->detector->qtp.max_nmaxima = 10;
    decoder->detector->qtp.deglitch = 0;
    decoder->detector->profile_now_us = apriltag_time_now_us;
    return 1;
}

void apriltag_decoder_close(apriltag_decoder_t *decoder)
{
    if(decoder->detector)
        apriltag_detector_destroy(decoder->detector);
    if(decoder->family)
        tag36h11_destroy(decoder->family);
    decoder->detector = NULL;
    decoder->family = NULL;
}

uint8_t apriltag_decoder_scan(apriltag_decoder_t *decoder, uint16_t width,
                              uint16_t height, uint8_t refine_edges,
                              const apriltag_scan_region_t *region,
                              apriltag_result_t items[APRILTAG_RESULT_MAX])
{
    image_u8_t image = {
        .width = region ? region->width : APRILTAG_INPUT_W,
        .height = region ? region->height : APRILTAG_INPUT_H,
        .stride = APRILTAG_INPUT_W,
        .buf = shared_luma(),
    };
    uint8_t count = 0U;
    decoder->detector->quad_decimate = region && region->full_detail ?
                                       1.0f : APRILTAG_QUAD_DECIMATE;
    decoder->detector->refine_edges = refine_edges != 0U;
    zarray_t *detections = apriltag_detector_detect(decoder->detector, &image);
    const double scale_x = region ? (double)APRILTAG_INPUT_W / width : 1.0;
    const double scale_y = region ? (double)APRILTAG_INPUT_H / height : 1.0;
    const uint16_t offset_x = region ? region->x : 0U;
    const uint16_t offset_y = region ? region->y : 0U;

    if(detections)
    {
        int detection_count = zarray_size(detections);

        for(int i = 0; i < detection_count && count < APRILTAG_RESULT_MAX; i++)
        {
            apriltag_detection_t *detection;
            apriltag_result_t *result = &items[count];
            int16_t min_x = (int16_t)width;
            int16_t min_y = (int16_t)height;
            int16_t max_x = 0;
            int16_t max_y = 0;
            int32_t confidence;

            zarray_get(detections, i, &detection);
            if(!detection || detection->id < 0 || detection->id > 65535)
                continue;
            for(uint8_t corner = 0; corner < 4U; corner++)
            {
                double canonical_x = (detection->p[corner][0] + offset_x) * scale_x;
                double canonical_y = (detection->p[corner][1] + offset_y) * scale_y;
                int16_t x = scaled_coordinate(canonical_x, APRILTAG_INPUT_W, width);
                int16_t y = scaled_coordinate(canonical_y, APRILTAG_INPUT_H, height);
                result->corners[corner][0] = (float)canonical_x;
                result->corners[corner][1] = (float)canonical_y;
                if(x < min_x) min_x = x;
                if(y < min_y) min_y = y;
                if(x > max_x) max_x = x;
                if(y > max_y) max_y = y;
            }
            confidence = (int32_t)(detection->decision_margin * 10.0);
            if(confidence < 0) confidence = 0;
            if(confidence > 1000) confidence = 1000;
            result->id = (uint16_t)detection->id;
            result->hamming = (uint8_t)detection->hamming;
            result->native_resolution = region != NULL;
            result->confidence = (uint16_t)confidence;
            result->x = min_x;
            result->y = min_y;
            result->w = max_x > min_x ? max_x - min_x : 1;
            result->h = max_y > min_y ? max_y - min_y : 1;
            result->center_x = scaled_coordinate((detection->c[0] + offset_x) * scale_x,
                                                  APRILTAG_INPUT_W, width);
            result->center_y = scaled_coordinate((detection->c[1] + offset_y) * scale_y,
                                                  APRILTAG_INPUT_H, height);
            count++;
        }
        apriltag_detections_destroy(detections);
    }
    return count;
}
