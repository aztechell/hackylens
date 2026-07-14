#include "app_sleep.h"

#include "../core/hk_app.h"
#include "../controllers/sleep_controller.h"

void sleep_enter(const hk_input_snapshot_t *input)
{
    sleep_controller_enter(input);
}

void sleep_handle_buttons(const hk_input_snapshot_t *input)
{
    sleep_controller_handle_buttons(input);
}
