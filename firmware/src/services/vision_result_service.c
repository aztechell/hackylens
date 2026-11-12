#include "vision_result_service.h"

#include <string.h>

static vision_result_snapshot_t g_snapshot;

vision_result_item_t vision_result_block(uint16_t id, uint8_t flags, uint16_t confidence,
                                         uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                         uint16_t sensor_height, uint8_t rotate)
{
    return (vision_result_item_t){
        .kind = VISION_ITEM_BLOCK,
        .flags = flags,
        .id = id,
        .confidence = confidence,
        .x0 = rotate ? sensor_height - y1 : x0,
        .y0 = rotate ? x0 : y0,
        .x1 = rotate ? sensor_height - y0 : x1,
        .y1 = rotate ? x1 : y1,
    };
}

void vision_result_publish(vision_source_t source, uint16_t width, uint16_t height,
                           const vision_result_item_t *items, uint8_t count)
{
    if(!items)
        count = 0U;
    else if(count > VISION_RESULT_MAX_ITEMS)
        count = VISION_RESULT_MAX_ITEMS;
    g_snapshot.frame_id++;
    g_snapshot.source = (uint8_t)source;
    g_snapshot.count = count;
    g_snapshot.width = width;
    g_snapshot.height = height;
    if(count)
        memcpy(g_snapshot.items, items, (size_t)count * sizeof(*items));
    if(count < VISION_RESULT_MAX_ITEMS)
        memset(g_snapshot.items + count, 0,
               (VISION_RESULT_MAX_ITEMS - count) * sizeof(g_snapshot.items[0]));
    memset(g_snapshot.qr_payload, 0, g_snapshot.qr_length);
    g_snapshot.qr_length = 0U;
}

void vision_result_publish_qr(uint16_t width, uint16_t height,
                              const vision_result_item_t *block,
                              const uint8_t *payload, uint16_t length)
{
    vision_result_publish(VISION_SOURCE_QR, width, height, block, 1U);
    if(block && payload && length)
    {
        if(length > VISION_RESULT_MAX_QR_BYTES)
            length = VISION_RESULT_MAX_QR_BYTES;
        memcpy(g_snapshot.qr_payload, payload, length);
        g_snapshot.qr_length = length;
    }
}

void vision_result_clear(vision_source_t source)
{
    if(source != VISION_SOURCE_NONE && g_snapshot.source != (uint8_t)source)
        return;
    vision_result_publish(VISION_SOURCE_NONE, 0U, 0U, NULL, 0U);
}

void hk_vision_read(vision_result_snapshot_t *snapshot)
{
    if(snapshot)
        *snapshot = g_snapshot;
}
