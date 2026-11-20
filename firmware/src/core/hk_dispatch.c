#include "hk_dispatch.h"

#include "hk_app.h"
#include "hk_events.h"

#include "../config/input_config.h"

#include "hk_app_registry.h"
#include "hk_menu.h"
#include "hk_screen.h"

static hk_sd_event_handler_t s_sd_event_handler;

void shell_set_sd_event_handler(hk_sd_event_handler_t handler)
{
    s_sd_event_handler = handler;
}

void shell_handle_buttons(const hk_input_snapshot_t *input)
{
    if(hk_screen_get() != SCREEN_MENU)
        return;

    uint32_t pressed = input->pressed;
    if(pressed & BUTTON_BACK)
    {
        menu_repeat_reset();
        menu_select_vertical();
        return;
    }
    if(input->state & BUTTON_BACK)
        return;

    if(pressed & BUTTON_LEFT)
    {
        menu_select_delta(-1);
        menu_repeat_start(BUTTON_LEFT);
    }
    if(pressed & BUTTON_RIGHT)
    {
        menu_select_delta(1);
        menu_repeat_start(BUTTON_RIGHT);
    }
    if(pressed & BUTTON_OK)
        shell_open_selected(input);
}

void shell_handle_sd_event(hk_sd_event_t event)
{
    if(s_sd_event_handler)
        s_sd_event_handler(event);
}
