#include "frame_pool.h"

#include <stddef.h>

#include "../config/camera_config.h"

static uint16_t g_camera_slots[FRAME_POOL_CAMERA_SLOT_COUNT][CAMERA_MAX_FRAME_PIXELS] __attribute__((aligned(64), section(".bss")));

uint16_t *frame_pool_camera_slot(uint8_t index)
{
    if(index >= FRAME_POOL_CAMERA_SLOT_COUNT)
        return NULL;
    return g_camera_slots[index];
}

uint32_t frame_pool_camera_frame_bytes(void)
{
    return sizeof(g_camera_slots[0]);
}

uint8_t *frame_pool_scratch_buffer(uint32_t min_size)
{
    if(min_size > sizeof(g_camera_slots[0]))
        return NULL;
    return (uint8_t *)g_camera_slots[0];
}
