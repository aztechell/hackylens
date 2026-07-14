#include "app_settings.h"

#include "../core/hk_app.h"
#include "../controllers/settings_controller.h"

void settings_enter(const hk_input_snapshot_t *input)
{
    settings_controller_enter(input);
}

void settings_handle_buttons(const hk_input_snapshot_t *input)
{
    settings_controller_handle_buttons(input);
}

void settings_tick(const hk_input_snapshot_t *input)
{
    settings_controller_tick(input);
}
