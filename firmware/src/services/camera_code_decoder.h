#ifndef HK_CAMERA_CODE_DECODER_H
#define HK_CAMERA_CODE_DECODER_H

#include <hackylens/capability/common.h>
#include <hackylens/capability/vision.h>

typedef struct
{
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint8_t rotate;
} camera_code_frame_t;

uint8_t camera_code_pass_count(vision_source_t source);
/* Stage on core 0 with the leased frame. The decoder works on luma on core 1. */
hk_result_t camera_code_stage(const void *owner, uint32_t lease, vision_source_t source,
                              uint8_t pass, uint8_t *image, camera_code_frame_t *metadata);
hk_result_t camera_code_prepare(vision_source_t source, uint8_t **image);
void camera_code_decode(vision_source_t source, const camera_code_frame_t *frame,
                         uint8_t *image, vision_result_snapshot_t *snapshot);
void camera_code_close(void);

#endif
