#ifndef HACKYLENS_CAPABILITY_DISPLAY_H
#define HACKYLENS_CAPABILITY_DISPLAY_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_DISPLAY UINT32_C(0x00010003)

#define HK_DISPLAY_FEATURE_BASE_PLANE (UINT64_C(1) << 0)
#define HK_DISPLAY_FEATURE_OVERLAY_PLANE (UINT64_C(1) << 1)
#define HK_DISPLAY_FEATURE_BATCH (UINT64_C(1) << 2)
#define HK_DISPLAY_FEATURE_DIRTY_REGIONS (UINT64_C(1) << 3)
#define HK_DISPLAY_FEATURE_RGB565 (UINT64_C(1) << 4)
#define HK_DISPLAY_FEATURE_BORROWED_SURFACE (UINT64_C(1) << 5)
#define HK_DISPLAY_FEATURE_TEXT (UINT64_C(1) << 6)
#define HK_DISPLAY_FEATURES_0_1                                      \
    (HK_DISPLAY_FEATURE_BASE_PLANE |                                \
     HK_DISPLAY_FEATURE_OVERLAY_PLANE | HK_DISPLAY_FEATURE_BATCH |  \
     HK_DISPLAY_FEATURE_DIRTY_REGIONS | HK_DISPLAY_FEATURE_RGB565 | \
     HK_DISPLAY_FEATURE_BORROWED_SURFACE | HK_DISPLAY_FEATURE_TEXT)

#define HK_DISPLAY_PLANE_BASE (UINT32_C(1) << 0)
#define HK_DISPLAY_PLANE_OVERLAY (UINT32_C(1) << 1)
#define HK_DISPLAY_PLANE_ALL \
    (HK_DISPLAY_PLANE_BASE | HK_DISPLAY_PLANE_OVERLAY)

#define HK_DISPLAY_FORMAT_RGB565_BE (UINT32_C(1) << 0)
#define HK_DISPLAY_LIMIT_WIDTH UINT32_C(1)
#define HK_DISPLAY_LIMIT_HEIGHT UINT32_C(2)
#define HK_DISPLAY_INFO_VERSION 1U
#define HK_DISPLAY_SURFACE_VERSION 1U

typedef struct
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} hk_display_rect_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_formats;
    uint32_t planes;
    uint32_t buffer_alignment_bytes;
    uint32_t row_alignment_bytes;
    uint16_t maximum_commands;
    uint16_t maximum_text_bytes;
    uint16_t maximum_dirty_rects;
    uint16_t maximum_borrowed_views;
    uint32_t transfer_slice_bytes;
    uint32_t maximum_present_duration_us;
    uint32_t reserved;
} hk_display_info_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    hk_buffer_view_t pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t reserved;
} hk_display_surface_t;

typedef struct hk_display_service hk_display_service_t;
/* A session must remain at its opening address until close or retire. */
typedef struct
{
    const hk_display_service_t *service;
    uint32_t plane;
} hk_display_t;

const hk_display_service_t *hk_display_service(void);
hk_result_t hk_display_open(
    const hk_display_service_t *service, uint32_t plane, hk_display_t *session);
hk_result_t hk_display_close(hk_display_t *session, hk_deadline_t deadline);
/* Always relinquishes the claim and borrowed references; failed cleanup
 * quarantines the plane until reset. */
hk_result_t hk_display_retire(hk_display_t *session, hk_deadline_t deadline);
hk_result_t hk_display_get_info(
    const hk_display_t *handle,
    hk_display_info_t *info);

hk_result_t hk_display_begin_batch(
    const hk_display_t *handle);
hk_result_t hk_display_set_clip(
    const hk_display_t *handle,
    const hk_display_rect_t *clip);
hk_result_t hk_display_clear(
    const hk_display_t *handle,
    uint16_t rgb565);
hk_result_t hk_display_fill_rect(
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565);
hk_result_t hk_display_stroke_rect(
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565);
hk_result_t hk_display_text(
    const hk_display_t *handle,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565);
hk_result_t hk_display_blit(
    const hk_display_t *handle,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format);
hk_result_t hk_display_mark_dirty(
    const hk_display_t *handle,
    const hk_display_rect_t *rect);

hk_result_t hk_display_surface_acquire(
    const hk_display_t *handle,
    hk_display_surface_t *surface);
hk_result_t hk_display_present(
    const hk_display_t *handle,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel);
hk_result_t hk_display_abort(
    const hk_display_t *handle);

#ifdef __cplusplus
}
#endif

#endif
