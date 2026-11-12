#ifndef HK_MICROPYTHON_CAMERA_CODES_H
#define HK_MICROPYTHON_CAMERA_CODES_H

#include <hackylens/capability/vision.h>
#include "micropython_capability_bridge.h"

typedef struct
{
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint8_t rotate;
} micropython_code_frame_t;

/* Core 0 stages luma while the camera lease is valid. The VM's core-1
 * job then runs the decoder on luma without retaining a camera pointer. */
hk_result_t micropython_camera_codes_stage(const void *owner, uint32_t lease,
                                           vision_source_t source, uint8_t mode,
                                           uint8_t *qr_image,
                                           micropython_code_frame_t *metadata);
micropython_binding_result_t micropython_camera_codes_scan(
    uint32_t lease, vision_source_t source, vision_result_snapshot_t *snapshot);
void micropython_camera_codes_cleanup(void);

#endif
