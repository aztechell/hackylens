#include "terminal_app.h"

#include "terminal_controller.h"
#include "terminal_view.h"

void terminal_enter(const hk_input_snapshot_t *input)
{
    terminal_controller_enter(input);
}

void terminal_exit(void)
{
    terminal_controller_exit();
}

void terminal_tick(const hk_input_snapshot_t *input)
{
    terminal_controller_tick(input);
}

void terminal_handle_buttons(const hk_input_snapshot_t *input)
{
    terminal_controller_handle_input(input);
}

void terminal_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    terminal_view_draw_icon(x, y, color, bg);
}

const hk_legacy_app_entry_t terminal_legacy_entry = {
    .screen = HK_TERMINAL_SCREEN,
    .enter = terminal_enter,
    .exit = terminal_exit,
    .tick = terminal_tick,
    .handle_input = terminal_handle_buttons,
    .draw_icon = terminal_draw_icon,
};
