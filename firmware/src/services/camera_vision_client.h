#ifndef HK_CAMERA_VISION_CLIENT_H
#define HK_CAMERA_VISION_CLIENT_H

#include <hackylens/capability/common.h>
#include <hackylens/capability/vision.h>

#include "ai_model_runtime.h"

/* Core 0. The model session belongs to the owner of the camera session. */
hk_result_t camera_vision_open_step(const void *owner, vision_source_t source,
                                   const char *package);
void camera_vision_close(const void *owner);
hk_result_t camera_vision_submit(const void *owner, uint32_t lease_id);
hk_result_t camera_vision_poll(const void *owner, vision_result_snapshot_t *snapshot);

#endif
