#include "buttons_controller.h"

#include <stdio.h>

#include "../core/hk_app.h"

#include "../config/input_config.h"

#include "../core/hk_back_exit.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../ui/buttons_view.h"

void buttons_controller_enter(const hk_input_snapshot_t *input)
{
    hk_screen_set(SCREEN_BUTTONS);
    hk_back_exit_set_armed(0);
    buttons_view_render(input->state);
    printf("[SHELL] screen BUTTONS\r\n");
}

void buttons_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(input->changed)
        buttons_view_update(input->changed, input->state);
    if(input->pressed & BUTTON_BACK)
        hk_back_exit_set_armed(1);
    if(hk_back_exit_armed() && (input->changed & BUTTON_BACK) && !(input->state & BUTTON_BACK))
        shell_show_menu();
}
