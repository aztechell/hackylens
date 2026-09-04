#include "sleep_view.h"

#include "sleep_view_assets.h"

hk_result_t sleep_view_render(hk_app_surface_t *surface)
{
    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    return hk_app_surface_clear(surface, COLOR_BLACK);
}
