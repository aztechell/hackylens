#include "surface_private.h"

#include <string.h>

static hk_result_t validate_surface(const hk_app_surface_t *surface)
{
    if(!surface || !surface->valid ||
       surface->struct_size != sizeof(*surface) ||
       surface->struct_version != HK_APP_SURFACE_VERSION ||
       surface->context_generation == 0U)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

hk_result_t hk_app_surface_private_init(
    hk_app_surface_t *surface,
    uint32_t context_generation,
    const hk_display_info_t *info,
    const hk_app_surface_ops_t *ops)
{
    if(!surface || !info || !ops || !ops->invalidate || !ops->clear ||
       !ops->fill_rect || !ops->stroke_rect || !ops->text || !ops->blit ||
       context_generation == 0U ||
       info->struct_size < sizeof(*info) ||
       info->struct_version != HK_DISPLAY_INFO_VERSION)
        return HK_ERR_INVALID_ARGUMENT;
    memset(surface, 0, sizeof(*surface));
    surface->struct_size = sizeof(*surface);
    surface->struct_version = HK_APP_SURFACE_VERSION;
    surface->context_generation = context_generation;
    surface->info = *info;
    surface->ops = *ops;
    surface->valid = 1U;
    return HK_OK;
}

void hk_app_surface_private_invalidate(hk_app_surface_t *surface)
{
    if(!surface)
        return;
    surface->valid = 0U;
    surface->ops = (hk_app_surface_ops_t){0};
}

hk_result_t hk_app_surface_get_info(
    const hk_app_surface_t *surface,
    hk_display_info_t *info)
{
    hk_result_t result;

    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_surface(surface);
    if(result != HK_OK)
        return result;
    *info = surface->info;
    return HK_OK;
}

hk_result_t hk_app_surface_invalidate(
    hk_app_surface_t *surface,
    const hk_display_rect_t *region)
{
    hk_result_t result = validate_surface(surface);

    if(result != HK_OK)
        return result;
    result = surface->ops.invalidate(surface->ops.user, region);
    if(result == HK_OK)
        surface->invalidated = 1U;
    return result;
}

#define HK_APP_SURFACE_CALL(name, ...)                                      \
    do                                                                       \
    {                                                                        \
        hk_result_t result = validate_surface(surface);                      \
        if(result != HK_OK)                                                  \
            return result;                                                   \
        return surface->ops.name(surface->ops.user, __VA_ARGS__);            \
    } while(0)

hk_result_t hk_app_surface_clear(
    hk_app_surface_t *surface,
    uint16_t rgb565)
{
    HK_APP_SURFACE_CALL(clear, rgb565);
}

hk_result_t hk_app_surface_fill_rect(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    if(!rect)
        return HK_ERR_INVALID_ARGUMENT;
    HK_APP_SURFACE_CALL(fill_rect, rect, rgb565);
}

hk_result_t hk_app_surface_stroke_rect(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    if(!rect)
        return HK_ERR_INVALID_ARGUMENT;
    HK_APP_SURFACE_CALL(stroke_rect, rect, rgb565);
}

hk_result_t hk_app_surface_text(
    hk_app_surface_t *surface,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    if(!bounds || !utf8)
        return HK_ERR_INVALID_ARGUMENT;
    HK_APP_SURFACE_CALL(text, bounds, utf8, size_bytes, rgb565);
}

hk_result_t hk_app_surface_blit(
    hk_app_surface_t *surface,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    if(!destination || !pixels)
        return HK_ERR_INVALID_ARGUMENT;
    HK_APP_SURFACE_CALL(blit, destination, pixels, pixel_format);
}

#undef HK_APP_SURFACE_CALL
