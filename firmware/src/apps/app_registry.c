#include "../core/hk_app_registry.h"

#include "../core/hk_app.h"

#include <stddef.h>

#include "hk_config.h"

#if HK_ENABLE_APP_TERMINAL
#include "terminal/terminal_app.h"
#endif
#if HK_ENABLE_APP_CAMERA
#include "app_camera.h"
#endif
#if HK_ENABLE_APP_QR_CAMERA
#include "app_qr_camera.h"
#endif
#if HK_ENABLE_APP_FACE_DETECT
#include "face_detect/face_detect_app.h"
#endif
#if HK_ENABLE_APP_APRILTAG
#include "apriltag/apriltag_app.h"
#endif
#if HK_ENABLE_APP_FILES
#include "app_files.h"
#endif
#if HK_ENABLE_APP_BUTTONS
#include "app_buttons.h"
#endif
#if HK_ENABLE_APP_PONG
#include "pong/pong_app.h"
#endif
#if HK_ENABLE_APP_SETTINGS
#include "app_settings.h"
#endif
#if HK_ENABLE_APP_SLEEP
#include "app_sleep.h"
#endif

const hk_app_t g_menu_items[] = {
#if HK_ENABLE_APP_TERMINAL
    {.id = "terminal", .title = "TERMINAL", .screen = HK_TERMINAL_SCREEN,
     .autostart_id = HK_AUTOSTART_TERMINAL,
     .enter = terminal_enter, .exit = terminal_exit, .tick = terminal_tick,
     .handle_input = terminal_handle_buttons, .draw_icon = terminal_draw_icon},
#endif
#if HK_ENABLE_APP_CAMERA
    {.id = "camera", .title = "CAMERA", .screen = SCREEN_CAMERA,
     .autostart_id = HK_AUTOSTART_CAMERA,
     .enter = camera_enter, .tick = camera_tick, .handle_input = camera_handle_buttons},
#endif
#if HK_ENABLE_APP_QR_CAMERA
    {.id = "qr_camera", .title = "QR-CAMERA", .screen = SCREEN_QR_CAMERA,
     .autostart_id = HK_AUTOSTART_QR_CAMERA,
     .enter = qr_camera_enter, .tick = qr_camera_tick, .handle_input = qr_camera_handle_buttons},
#endif
#if HK_ENABLE_APP_FACE_DETECT
    {.id = "face_detect", .title = "FACE DETECT", .screen = SCREEN_FACE_DETECT,
     .autostart_id = HK_AUTOSTART_FACE_DETECT,
     .enter = face_detect_enter, .exit = face_detect_exit, .tick = face_detect_tick,
     .handle_input = face_detect_handle_buttons, .draw_icon = face_detect_draw_icon,
     .background_tick = face_detect_background_tick,
     .handle_debug_command = face_detect_handle_debug_command,
     .debug_help = g_face_detect_debug_help},
#endif
#if HK_ENABLE_APP_APRILTAG
    {.id = "apriltag", .title = "APRILTAG", .screen = SCREEN_APRILTAG,
     .autostart_id = HK_AUTOSTART_APRILTAG,
     .enter = apriltag_enter, .exit = apriltag_exit, .tick = apriltag_tick,
     .handle_input = apriltag_handle_buttons, .draw_icon = apriltag_draw_icon,
     .handle_debug_command = apriltag_handle_debug_command,
     .debug_help = g_apriltag_debug_help},
#endif
#if HK_ENABLE_APP_FILES
    {.id = "files", .title = "FILES", .screen = SCREEN_FILES,
     .autostart_id = HK_AUTOSTART_FILES,
     .enter = files_enter, .exit = files_exit, .tick = files_tick,
     .handle_input = files_handle_buttons},
#endif
#if HK_ENABLE_APP_BUTTONS
    {.id = "buttons", .title = "BUTTONS", .screen = SCREEN_BUTTONS,
     .autostart_id = HK_AUTOSTART_BUTTONS,
     .enter = buttons_enter, .handle_input = buttons_handle_buttons},
#endif
#if HK_ENABLE_APP_PONG
    {.id = "pong", .title = "PONG", .screen = HK_PONG_SCREEN,
     .autostart_id = HK_AUTOSTART_PONG,
     .enter = pong_enter, .tick = pong_tick, .handle_input = pong_handle_buttons,
     .draw_icon = pong_draw_icon},
#endif
#if HK_ENABLE_APP_SETTINGS
    {.id = "settings", .title = "SETTINGS", .screen = SCREEN_SETTINGS,
     .enter = settings_enter, .exit = settings_exit, .tick = settings_tick,
     .handle_input = settings_handle_buttons},
#endif
#if HK_ENABLE_APP_SLEEP
    {.id = "sleep", .title = "SLEEP", .screen = SCREEN_SLEEP,
     .enter = sleep_enter, .handle_input = sleep_handle_buttons},
#endif
};

const uint8_t g_menu_item_count = (uint8_t)(sizeof(g_menu_items) / sizeof(g_menu_items[0]));

const hk_app_t *hk_app_for_screen(screen_t screen)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].screen == screen)
            return &g_menu_items[i];
    }
    return NULL;
}

const hk_app_t *hk_app_for_autostart_id(hk_autostart_id_t id)
{
    if(id <= HK_AUTOSTART_OFF || id >= HK_AUTOSTART_COUNT)
        return NULL;
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].autostart_id == id)
            return &g_menu_items[i];
    }
    return NULL;
}

uint8_t hk_app_autostart_count(void)
{
    uint8_t count = 0U;

    for(uint8_t i = 0; i < g_menu_item_count; i++)
        count += g_menu_items[i].autostart_id != HK_AUTOSTART_OFF ? 1U : 0U;
    return count;
}

const hk_app_t *hk_app_autostart_at(uint8_t index)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].autostart_id == HK_AUTOSTART_OFF)
            continue;
        if(index == 0U)
            return &g_menu_items[i];
        index--;
    }
    return NULL;
}

void hk_app_registry_background_tick(void)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].background_tick)
            g_menu_items[i].background_tick();
    }
}

uint8_t hk_app_registry_handle_debug_command(const char *cmd)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].handle_debug_command && g_menu_items[i].handle_debug_command(cmd))
            return 1;
    }
    return 0;
}
