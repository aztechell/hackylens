#include "camera_app.h"

#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "camera_controller.h"
#include "camera_view.h"

void camera_enter(const hk_input_snapshot_t *input) { camera_controller_enter(input); }
void camera_exit(void) { camera_controller_exit(); }
void camera_tick(const hk_input_snapshot_t *input) { camera_controller_tick(input); }
void camera_handle_buttons(const hk_input_snapshot_t *input) { camera_controller_handle_input(input); }

uint8_t camera_owns_screen(screen_t screen)
{
    return screen == SCREEN_CAMERA_SETTINGS && camera_controller_settings_active();
}

uint8_t camera_handle_debug_command(const char *cmd)
{
    if(!str_eq_ci(cmd, "HKCAMERA") && !str_eq_ci(cmd, "HKCAM"))
        return 0U;
    activity_note();
    if(hk_screen_get() != SCREEN_MENU)
        shell_show_menu();
    camera_controller_enter(NULL);
    return 1U;
}

void camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    camera_view_draw_icon(x, y, color, bg);
}

const hk_legacy_app_entry_t camera_legacy_entry = {
    .screen = SCREEN_CAMERA,
    .enter = camera_enter,
    .exit = camera_exit,
    .tick = camera_tick,
    .handle_input = camera_handle_buttons,
    .owns_screen = camera_owns_screen,
    .draw_icon = camera_draw_icon,
    .blocks_sd_poll = 1U,
    .handle_debug_command = camera_handle_debug_command,
};
