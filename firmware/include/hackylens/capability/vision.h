#ifndef HACKYLENS_CAPABILITY_VISION_H
#define HACKYLENS_CAPABILITY_VISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_RESULT_MAX_ITEMS 8U
#define VISION_RESULT_MAX_QR_BYTES 383U

typedef enum
{
    VISION_ITEM_BLOCK = 1,
    VISION_ITEM_ARROW = 2,
} vision_item_kind_t;

typedef enum
{
    VISION_SOURCE_NONE = 0,
    VISION_SOURCE_FACE = 1,
    VISION_SOURCE_QR = 2,
    VISION_SOURCE_APRILTAG = 3,
    VISION_SOURCE_OBJECT_DETECT = 4,
    VISION_SOURCE_FACE_RECOGNITION = 5,
    VISION_SOURCE_USER = 255,
} vision_source_t;

typedef struct
{
    uint8_t kind;
    uint8_t flags;
    uint16_t id;
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint16_t confidence;
    uint16_t reserved;
} vision_result_item_t;

typedef struct
{
    uint32_t frame_id;
    uint8_t source;
    uint8_t count;
    uint16_t width;
    uint16_t height;
    vision_result_item_t items[VISION_RESULT_MAX_ITEMS];
    uint16_t qr_length;
    uint8_t qr_payload[VISION_RESULT_MAX_QR_BYTES];
} vision_result_snapshot_t;

/* Core 0 only. Copies the latest publication. The frame ID advances on each
 * publication or clear. Coordinates are relative to the source canvas. */
void hk_vision_read(vision_result_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
