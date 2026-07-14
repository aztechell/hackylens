#include "app_buttons.h"

#include "../core/hk_app.h"
#include "../controllers/buttons_controller.h"

void buttons_enter(const hk_input_snapshot_t *input)
{
    buttons_controller_enter(input);
}

void buttons_handle_buttons(const hk_input_snapshot_t *input)
{
    buttons_controller_handle_buttons(input);
}
