#ifndef HK_CAMERA_CLIENT_H
#define HK_CAMERA_CLIENT_H

#include <hackylens/capability/common.h>

#include "../core/color_tracker.h"
#include "camera_frame.h"

/* Core 0 only. An owner may hold one frame until release or close.
 * C clients may read pixels while the frame is held. Language adapters
 * should expose only the lease ID and dimensions to their callers. */
hk_result_t camera_client_open(const void *owner);
uint8_t camera_client_is_open(const void *owner);
void camera_client_close(const void *owner);
void camera_client_tick(const void *owner);
hk_result_t camera_client_acquire(const void *owner, camera_preview_frame_t *frame);
hk_result_t camera_client_get(const void *owner, uint32_t lease_id,
                              camera_preview_frame_t *frame);
hk_result_t camera_client_release(const void *owner, uint32_t lease_id);
hk_result_t camera_client_find_color(
    const void *owner, uint32_t lease_id, const hk_color_rect_t *roi,
    const hk_color_threshold_t *threshold, uint32_t min_pixels, hk_color_blob_t *blob);

#endif
