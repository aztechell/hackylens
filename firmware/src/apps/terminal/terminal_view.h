#ifndef HK_TERMINAL_VIEW_H
#define HK_TERMINAL_VIEW_H

#include <hackylens/app.h>

#include "terminal_types.h"

terminal_geometry_t terminal_view_geometry(terminal_font_size_t font_size);
hk_result_t terminal_view_render(
    hk_app_surface_t *surface, terminal_font_size_t font_size);

#endif
