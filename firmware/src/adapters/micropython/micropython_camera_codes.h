#ifndef HK_MICROPYTHON_CAMERA_CODES_H
#define HK_MICROPYTHON_CAMERA_CODES_H

#include <hackylens/capability/vision.h>
#include "micropython_capability_bridge.h"

micropython_binding_result_t micropython_camera_codes_scan(
    uint32_t lease, vision_source_t source, vision_result_snapshot_t *snapshot);
void micropython_camera_codes_cleanup(void);

#endif
