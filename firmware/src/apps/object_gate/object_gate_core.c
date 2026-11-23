#include "object_gate_core.h"

#include <stddef.h>

enum { TARGET_CLASS = 14, MIN_CONFIDENCE = 500, STABLE_FRAMES = 3 };

object_gate_view_t object_gate_step(object_gate_state_t *gate,
                                   const vision_result_snapshot_t *snapshot)
{
    const uint16_t left = snapshot->width / 4U, top = snapshot->height / 4U;
    const uint16_t right = snapshot->width - left, bottom = snapshot->height - top;
    const vision_result_item_t *best = NULL;
    uint16_t score = MIN_CONFIDENCE - 1U;
    for(uint8_t index = 0; index < snapshot->count; ++index)
    {
        const vision_result_item_t *item = &snapshot->items[index];
        if(item->kind != VISION_ITEM_BLOCK || item->id != TARGET_CLASS || item->confidence <= score)
            continue;
        if(item->x0 >= item->x1 || item->x1 > snapshot->width ||
           item->y0 >= item->y1 || item->y1 > snapshot->height)
            continue;
        uint16_t cx = ((uint32_t)item->x0 + item->x1) / 2U;
        uint16_t cy = ((uint32_t)item->y0 + item->y1) / 2U;
        if(cx >= left && cx < right && cy >= top && cy < bottom)
        {
            best = item;
            score = item->confidence;
        }
    }
    if(!best)
    {
        gate->hits = 0U;
        if(gate->misses < STABLE_FRAMES)
            gate->misses++;
        if(gate->misses == STABLE_FRAMES)
            gate->occupied = 0U;
        return (object_gate_view_t){{left, top, right - left, bottom - top}, 0x07ffU};
    }

    gate->misses = 0U;
    if(gate->hits < STABLE_FRAMES)
        gate->hits++;
    if(gate->hits == STABLE_FRAMES && !gate->occupied)
    {
        gate->occupied = 1U;
        gate->entries++;
    }
    return (object_gate_view_t){{best->x0, best->y0, best->x1 - best->x0, best->y1 - best->y0},
                                gate->hits == STABLE_FRAMES ? 0x07e0U : 0xfd20U};
}

uint32_t object_gate_digest(uint32_t digest, const vision_result_snapshot_t *snapshot)
{
    digest = digest * 33U + snapshot->width;
    digest = digest * 33U + snapshot->height;
    digest = digest * 33U + snapshot->count;
    for(uint8_t index = 0; index < snapshot->count; ++index)
    {
        const vision_result_item_t *item = &snapshot->items[index];
        const uint16_t values[] = {item->kind, item->flags, item->id, item->x0, item->y0,
                                   item->x1, item->y1, item->confidence};
        for(unsigned field = 0; field < sizeof(values) / sizeof(values[0]); ++field)
            digest = digest * 33U + values[field];
    }
    return digest;
}
