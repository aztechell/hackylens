#ifndef HK_OBJECT_GATE_CORE_H
#define HK_OBJECT_GATE_CORE_H

#include <hackylens/capability/camera_types.h>
#include <hackylens/capability/vision.h>

typedef struct
{
    uint32_t entries;
    uint8_t hits;
    uint8_t misses;
    uint8_t occupied;
} object_gate_state_t;

typedef struct
{
    hk_color_rect_t box;
    uint16_t color;
} object_gate_view_t;

object_gate_view_t object_gate_step(object_gate_state_t *gate,
                                   const vision_result_snapshot_t *snapshot);
uint32_t object_gate_digest(uint32_t digest, const vision_result_snapshot_t *snapshot);

#endif
