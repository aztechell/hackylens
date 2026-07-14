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
#include "app_face_detect.h"
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
    {"terminal", "TERMINAL", HK_TERMINAL_SCREEN, terminal_enter, terminal_exit, terminal_tick, terminal_handle_buttons, terminal_draw_icon},
#endif
#if HK_ENABLE_APP_CAMERA
    {"camera", "CAMERA", SCREEN_CAMERA, camera_enter, NULL, camera_tick, camera_handle_buttons, NULL},
#endif
#if HK_ENABLE_APP_QR_CAMERA
    {"qr_camera", "QR-CAMERA", SCREEN_QR_CAMERA, qr_camera_enter, NULL, qr_camera_tick, qr_camera_handle_buttons, NULL},
#endif
#if HK_ENABLE_APP_FACE_DETECT
    {"face_detect", "FACE DETECT", SCREEN_FACE_DETECT, face_detect_enter, NULL, face_detect_tick, face_detect_handle_buttons, NULL},
#endif
#if HK_ENABLE_APP_FILES
    {"files", "FILES", SCREEN_FILES, files_enter, NULL, files_tick, files_handle_buttons, NULL},
#endif
#if HK_ENABLE_APP_BUTTONS
    {"buttons", "BUTTONS", SCREEN_BUTTONS, buttons_enter, NULL, NULL, buttons_handle_buttons, NULL},
#endif
#if HK_ENABLE_APP_PONG
    {"pong", "PONG", HK_PONG_SCREEN, pong_enter, NULL, pong_tick, pong_handle_buttons, pong_draw_icon},
#endif
#if HK_ENABLE_APP_SETTINGS
    {"settings", "SETTINGS", SCREEN_SETTINGS, settings_enter, NULL, settings_tick, settings_handle_buttons, NULL},
#endif
#if HK_ENABLE_APP_SLEEP
    {"sleep", "SLEEP", SCREEN_SLEEP, sleep_enter, NULL, NULL, sleep_handle_buttons, NULL},
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
