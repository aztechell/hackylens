#include "settings_app.h"

#include "../../core/hk_app.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "settings_controller.h"
#include "settings_view.h"

void settings_enter(const hk_input_snapshot_t *input)
{
    settings_controller_enter(input);
}

void settings_exit(void)
{
    settings_controller_exit();
}

void settings_handle_buttons(const hk_input_snapshot_t *input)
{
    settings_controller_handle_buttons(input);
}

void settings_tick(const hk_input_snapshot_t *input)
{
    settings_controller_tick(input);
}

uint8_t settings_handle_debug_command(const char *cmd)
{
    if(!str_eq_ci(cmd, "HKSETTINGS"))
        return 0U;
    activity_note();
    if(hk_screen_get() != SCREEN_MENU)
        shell_show_menu();
    settings_controller_enter(NULL);
    return 1U;
}

void settings_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    settings_view_draw_icon(x, y, color, bg);
}

const hk_legacy_app_entry_t settings_legacy_entry = {
    .screen = SCREEN_SETTINGS,
    .enter = settings_enter,
    .exit = settings_exit,
    .tick = settings_tick,
    .handle_input = settings_handle_buttons,
    .draw_icon = settings_draw_icon,
    .handle_debug_command = settings_handle_debug_command,
};
