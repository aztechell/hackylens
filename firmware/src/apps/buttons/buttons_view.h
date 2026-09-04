#ifndef BUTTONS_VIEW_H
#define BUTTONS_VIEW_H

#include <hackylens/app.h>

#include "buttons_controller.h"

hk_result_t buttons_view_render(
    hk_app_surface_t *surface, const buttons_view_state_t *state);

#endif
