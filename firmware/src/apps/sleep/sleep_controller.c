#include "sleep_controller.h"

#include <stdio.h>

#include "../../core/hk_app.h"

#include "../../core/hk_back_exit.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../services/settings_lights.h"
#include "../../services/settings_service.h"
#include "../../hal/hal_time.h"
#include "sleep_view.h"

void sleep_controller_enter(const hk_input_snapshot_t *input)
{
    hk_screen_set(SCREEN_SLEEP);
    hk_back_exit_set_armed(0);
    sleep_view_enter();
    screen_brightness_off();
    printf("[SHELL] screen SLEEP\r\n");
    printf("[SLEEP] enter\r\n");
}

static void sleep_wake(void)
{
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    printf("[SLEEP] wake\r\n");
    shell_show_menu();
}

void sleep_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(input->pressed)
        sleep_wake();
}

void auto_sleep_controller_tick(const hk_input_snapshot_t *input)
{
    uint64_t now;
    uint64_t timeout_us;
    uint8_t auto_sleep_minutes = hk_auto_sleep_minutes();

    if(hk_screen_get() != SCREEN_MENU || input->state || auto_sleep_minutes == 0)
        return;

    now = hal_time_us();
    timeout_us = (uint64_t)auto_sleep_minutes * 60ULL * 1000000ULL;
    if(hk_last_activity_us() != 0 && now - hk_last_activity_us() >= timeout_us)
    {
        printf("[SLEEP] auto after %u min\r\n", auto_sleep_minutes);
        sleep_controller_enter(input);
    }
}
