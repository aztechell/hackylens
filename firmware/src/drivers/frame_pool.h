#ifndef FRAME_POOL_H
#define FRAME_POOL_H

#include <stdint.h>

#define FRAME_POOL_CAMERA_SLOT_COUNT 2U

uint16_t *frame_pool_camera_slot(uint8_t index);
uint32_t frame_pool_camera_frame_bytes(void);
uint8_t *frame_pool_scratch_buffer(uint32_t min_size);

#endif
