#include "autostart_controller.h"

#include <stdio.h>

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_menu.h"
#include "../services/settings_service.h"

void autostart_controller_start(void)
{
    static const hk_input_snapshot_t startup_input = {0U, 0U, 0U};
    hk_autostart_id_t id = settings_autostart_id();
    const hk_app_t *app;

    if(id == HK_AUTOSTART_OFF)
    {
        printf("[BOOT] autostart=OFF\r\n");
        shell_show_menu();
        return;
    }

    app = hk_app_for_autostart_id(id);
    if(!app)
    {
        printf("[BOOT] autostart=%u unavailable, fallback=MENU\r\n", (unsigned)id);
        shell_show_menu();
        return;
    }

    printf("[BOOT] autostart=%u app=%s\r\n", (unsigned)id, app->title);
    if(!shell_open_app(app, &startup_input))
    {
        printf("[BOOT] autostart=%u open failed, fallback=MENU\r\n", (unsigned)id);
        shell_show_menu();
    }
}
