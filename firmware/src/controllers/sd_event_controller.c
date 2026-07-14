#include "sd_event_controller.h"

#include "../core/hk_app.h"
#include "../core/hk_events.h"

#include "hk_config.h"
#if HK_ENABLE_APP_FILES
#include "files_controller.h"
#include "files_actions.h"
#include "files_presenter.h"
#endif
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../storage/fat32_volume.h"
#if HK_ENABLE_APP_FILES
#endif
#include "../ui/hk_ui.h"

void sd_event_controller_handle(hk_sd_event_t event)
{
#if HK_ENABLE_APP_FILES
    uint8_t files_ready;
#endif
    screen_t screen;

    if(event == HK_SD_EVENT_NONE)
        return;

#if HK_ENABLE_APP_FILES
    files_controller_reset_input();
    files_ready = files_on_sd_event(event);
#endif
    topbar_set_sd_mounted((uint8_t)(hk_sd_present() && hk_fat_mounted()));
    screen = hk_screen_get();
    if(screen == SCREEN_MENU)
    {
        menu_render();
        return;
    }

#if HK_ENABLE_APP_FILES
    if(screen != SCREEN_FILES)
        return;

    if(!hk_sd_present() || !hk_fat_mounted())
    {
        files_presenter_show_status("NO SD");
        return;
    }

    if(!files_ready)
    {
        files_presenter_show_result(FILE_RESULT_READ_ERROR);
        return;
    }

    files_presenter_render_list();
#endif
}
