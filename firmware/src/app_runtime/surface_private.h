#ifndef HK_APP_RUNTIME_SURFACE_PRIVATE_H
#define HK_APP_RUNTIME_SURFACE_PRIVATE_H

#include <hackylens/app/runtime.h>

typedef struct
{
    void *user;
    hk_result_t (*invalidate)(void *user, const hk_display_rect_t *region);
    hk_result_t (*clear)(void *user, uint16_t rgb565);
    hk_result_t (*fill_rect)(
        void *user, const hk_display_rect_t *rect, uint16_t rgb565);
    hk_result_t (*stroke_rect)(
        void *user, const hk_display_rect_t *rect, uint16_t rgb565);
    hk_result_t (*text)(
        void *user, const hk_display_rect_t *bounds, const char *utf8,
        uint32_t size_bytes, uint16_t rgb565);
    hk_result_t (*blit)(
        void *user, const hk_display_rect_t *destination,
        const hk_buffer_view_t *pixels, uint32_t pixel_format);
} hk_app_surface_ops_t;

struct hk_app_surface
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t context_generation;
    hk_display_info_t info;
    hk_app_surface_ops_t ops;
    uint8_t valid;
    uint8_t invalidated;
};

hk_result_t hk_app_surface_private_init(
    hk_app_surface_t *surface,
    uint32_t context_generation,
    const hk_display_info_t *info,
    const hk_app_surface_ops_t *ops);
void hk_app_surface_private_invalidate(hk_app_surface_t *surface);

#endif
