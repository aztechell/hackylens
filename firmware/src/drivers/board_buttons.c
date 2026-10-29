#include "hk_input.h"

#include "board_config.h"
#include <hackylens/capability/input.h>

#include "hal_gpio.h"

uint32_t buttons_read_pressed_mask(void)
{
    uint32_t raw = 0;
    raw |= hal_gpiohs_read(GPIOHS_BTN_LEFT) ? HK_INPUT_BUTTON_LEFT : 0U;
    raw |= hal_gpiohs_read(GPIOHS_BTN_OK) ? HK_INPUT_BUTTON_OK : 0U;
    raw |= hal_gpiohs_read(GPIOHS_BTN_RIGHT) ? HK_INPUT_BUTTON_RIGHT : 0U;
#if !HK_BOARD_INPUT_PROFILE_CUBE
    raw |= hal_gpiohs_read(GPIOHS_BTN_BACK) ? HK_INPUT_BUTTON_BACK : 0U;
    return (~raw) & HK_INPUT_BUTTON_ALL;
#else
    /* Center is physical OK here; the input adapter resolves tap/hold. */
    return (~raw) & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK |
                     HK_INPUT_BUTTON_RIGHT);
#endif
}

uint8_t buttons_boot_override_pressed(void)
{
#if HK_BOARD_INPUT_PROFILE_CUBE
    return (buttons_read_pressed_mask() & HK_INPUT_BUTTON_OK) != 0U;
#else
    return (buttons_read_pressed_mask() & HK_INPUT_BUTTON_BACK) != 0U;
#endif
}
