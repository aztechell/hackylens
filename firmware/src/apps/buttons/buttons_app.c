#include "buttons_app.h"

#include "../../core/hk_app.h"
#include "buttons_controller.h"
#include "buttons_view.h"

void buttons_enter(const hk_input_snapshot_t *input)
{
    buttons_controller_enter(input);
}

void buttons_tick(const hk_input_snapshot_t *input)
{
    buttons_controller_tick(input);
}

void buttons_handle_buttons(const hk_input_snapshot_t *input)
{
    buttons_controller_handle_buttons(input);
}

void buttons_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    buttons_view_draw_icon(x, y, color, bg);
}

const hk_legacy_app_entry_t buttons_legacy_entry = {
    .screen = SCREEN_BUTTONS,
    .enter = buttons_enter,
    .tick = buttons_tick,
    .handle_input = buttons_handle_buttons,
    .draw_icon = buttons_draw_icon,
};
