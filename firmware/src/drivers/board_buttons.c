#include "hk_input.h"

#include "../core/hk_app.h"

#include "../board/board_pins.h"
#include "../config/input_config.h"

#include "../hal/hal_gpio.h"

static uint32_t g_buttons_state;
static uint32_t g_buttons_pressed;
static uint32_t g_buttons_changed;
static uint32_t g_buttons_last_sample;
static uint8_t g_buttons_same_count;

uint32_t hk_input_state(void)
{
    return g_buttons_state;
}

uint32_t hk_input_pressed(void)
{
    return g_buttons_pressed;
}

uint32_t hk_input_changed(void)
{
    return g_buttons_changed;
}

hk_input_snapshot_t hk_input_current(void)
{
    hk_input_snapshot_t snapshot;
    snapshot.state = g_buttons_state;
    snapshot.pressed = g_buttons_pressed;
    snapshot.changed = g_buttons_changed;
    return snapshot;
}

hk_input_snapshot_t hk_input_poll(void)
{
    buttons_poll();
    return hk_input_current();
}

uint32_t buttons_read_pressed_mask(void)
{
    uint32_t raw = 0;
    raw |= hal_gpiohs_read(GPIOHS_BTN_LEFT) ? BUTTON_LEFT : 0;
    raw |= hal_gpiohs_read(GPIOHS_BTN_OK) ? BUTTON_OK : 0;
    raw |= hal_gpiohs_read(GPIOHS_BTN_RIGHT) ? BUTTON_RIGHT : 0;
    raw |= hal_gpiohs_read(GPIOHS_BTN_BACK) ? BUTTON_BACK : 0;
    return (~raw) & BUTTON_ALL;
}

void buttons_sync(void)
{
    g_buttons_state = buttons_read_pressed_mask();
    g_buttons_last_sample = g_buttons_state;
    g_buttons_same_count = BUTTON_DEBOUNCE_POLLS;
    g_buttons_pressed = 0;
    g_buttons_changed = 0;
}

void buttons_poll(void)
{
    uint32_t sample = buttons_read_pressed_mask();
    g_buttons_pressed = 0;
    g_buttons_changed = 0;

    if(sample != g_buttons_last_sample)
    {
        g_buttons_last_sample = sample;
        g_buttons_same_count = 0;
        return;
    }

    if(g_buttons_same_count < BUTTON_DEBOUNCE_POLLS)
    {
        g_buttons_same_count++;
        if(g_buttons_same_count == BUTTON_DEBOUNCE_POLLS && sample != g_buttons_state)
        {
            g_buttons_changed = sample ^ g_buttons_state;
            g_buttons_pressed = g_buttons_changed & sample;
            g_buttons_state = sample;
        }
    }
}
