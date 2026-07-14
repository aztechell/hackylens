#include "hk_main.h"

#include <string.h>

#include "../core/hk_app_registry.h"
#include "../core/hk_dispatch.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../drivers/hk_input.h"
#include "../hal/hal_time.h"

static hk_main_hooks_t s_hooks;

void hk_main_set_hooks(const hk_main_hooks_t *hooks)
{
    if(hooks)
        s_hooks = *hooks;
    else
        memset(&s_hooks, 0, sizeof(s_hooks));
}

int hk_main(void)
{
    if(s_hooks.startup)
        s_hooks.startup();
    while(1)
    {
        const hk_app_t *app;
        hk_input_snapshot_t input;

        if(s_hooks.debug_tick)
            s_hooks.debug_tick();
        input = hk_input_poll();
        if(input.changed || input.pressed)
        {
            activity_note();
            shell_handle_buttons(&input);
        }
        if(hk_screen_get() == SCREEN_MENU)
            menu_tick(&input);
        app = hk_app_for_screen(hk_screen_get());
        if(app && app->tick)
            app->tick(&input);
        if(s_hooks.system_tick)
            s_hooks.system_tick(&input);
        hal_sleep_ms((hk_screen_get() == SCREEN_CAMERA || hk_screen_get() == SCREEN_QR_CAMERA || hk_screen_get() == SCREEN_FACE_DETECT) ? 1 : 20);
    }

    return 0;
}
