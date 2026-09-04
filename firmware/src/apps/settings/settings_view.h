#ifndef HK_SETTINGS_VIEW_H
#define HK_SETTINGS_VIEW_H

#include <hackylens/app.h>

#include "settings_menu.h"

hk_result_t settings_view_render(
    hk_app_surface_t *surface, const settings_menu_session_t *session);

#endif
