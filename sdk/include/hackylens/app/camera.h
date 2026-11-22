#ifndef HACKYLENS_APP_CAMERA_H
#define HACKYLENS_APP_CAMERA_H

#include <hackylens/app/context.h>
#include <hackylens/app/data.h>
#include <hackylens/capability/camera_types.h>
#include <hackylens/capability/vision.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t lease;
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
} hk_app_camera_frame_t;

/* Declare camera in app.toml. One foreground app may hold one frame.
 * All calls use the live callback context. The runtime closes the camera
 * and releases any held frame during teardown. */
hk_result_t hk_app_camera_open(const hk_app_context_t *ctx);
hk_result_t hk_app_camera_close(const hk_app_context_t *ctx);
/* HK_PENDING means capture has not provided a new frame yet. */
hk_result_t hk_app_camera_acquire(const hk_app_context_t *ctx,
                                  hk_app_camera_frame_t *frame);
hk_result_t hk_app_camera_release(const hk_app_context_t *ctx, uint32_t lease);
/* NULL roi selects the whole frame. HK_OK with zero pixels means no match. */
hk_result_t hk_app_camera_find_color(const hk_app_context_t *ctx, uint32_t lease,
                                     const hk_color_rect_t *roi,
                                     const hk_color_threshold_t *threshold,
                                     uint32_t min_pixels, hk_color_blob_t *blob);
/* Call from an event in an app using camera-owned presentation (render = NULL).
 * The frame is fitted to the display. box may be NULL. */
hk_result_t hk_app_camera_present(const hk_app_context_t *ctx, uint32_t lease,
                                  const hk_color_rect_t *box, uint16_t color);

/* Declare sd-card to save a BMP24 image in /PYDATA. Repeat begin with the
 * same lease and name on HK_PENDING. After HK_OK, call step until HK_OK
 * publishes the file. Keep the frame until the save finishes or is cancelled.
 * The result codes in data.h also apply to camera saves. */
hk_result_t hk_app_camera_save_begin(const hk_app_context_t *ctx, uint32_t lease,
                                     const char *name, size_t length);
hk_result_t hk_app_camera_save_step(const hk_app_context_t *ctx);
hk_result_t hk_app_camera_save_cancel(const hk_app_context_t *ctx);

/* Declare qr-decoder or apriltag-decoder for the corresponding source.
 * Begin returns HK_OK when accepted. Poll until it returns HK_OK with the
 * result, including an empty snapshot for no match. Keep the lease while
 * polling. After cancellation the app can release the frame. */
hk_result_t hk_app_camera_scan_begin(const hk_app_context_t *ctx, uint32_t lease,
                                     vision_source_t source);
hk_result_t hk_app_camera_scan_poll(const hk_app_context_t *ctx,
                                    vision_result_snapshot_t *snapshot);
hk_result_t hk_app_camera_scan_cancel(const hk_app_context_t *ctx);

/* Declare sd-card and face-detector or object-detector for model loading.
 * Repeat open with the same arguments while HK_PENDING. NULL selects the
 * default installed model. Keep the lease from a successful detect_begin
 * until polling finishes or the model closes. */
hk_result_t hk_app_camera_model_open_step(const hk_app_context_t *ctx,
                                          vision_source_t source, const char *package);
hk_result_t hk_app_camera_model_close(const hk_app_context_t *ctx);
hk_result_t hk_app_camera_detect_begin(const hk_app_context_t *ctx, uint32_t lease);
hk_result_t hk_app_camera_detect_poll(const hk_app_context_t *ctx,
                                     vision_result_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
