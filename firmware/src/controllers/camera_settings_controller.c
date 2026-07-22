#include "camera_settings_controller.h"

#include <stdio.h>

#include "camera_settings_menu.h"
#include "settings_menu_controller.h"
#include "../config/input_config.h"
#include "../core/hk_back_exit.h"
#include "../core/hk_screen.h"
#include "../services/camera_session.h"

static settings_menu_session_t g_camera_settings_menu;
static uint8_t g_camera_settings_qr_mode;

static void camera_settings_enter_common(uint8_t qr_mode)
{
    g_camera_settings_qr_mode = qr_mode ? 1U : 0U;
    hk_screen_set(SCREEN_CAMERA_SETTINGS);
    hk_back_exit_set_armed(0U);
    (void)settings_menu_open(&g_camera_settings_menu,
                             camera_settings_menu_definition(g_camera_settings_qr_mode));
    printf("[SHELL] screen %s SETTINGS\r\n", g_camera_settings_qr_mode ? "QR" : "CAMERA");
}

void camera_settings_enter(void)
{
    camera_settings_enter_common(0U);
}

void qr_settings_enter(void)
{
    camera_service_freeze(1U);
    camera_settings_enter_common(1U);
}

uint8_t camera_settings_is_qr(void)
{
    return g_camera_settings_qr_mode;
}

static camera_settings_exit_t camera_settings_exit(uint32_t input_state)
{
    camera_settings_return_t action;
    uint8_t qr_mode = g_camera_settings_qr_mode;

    settings_menu_close(&g_camera_settings_menu);
    action = camera_service_prepare_settings_return(qr_mode,
                                                    (input_state & BUTTON_OK) ? 1U : 0U);
    g_camera_settings_qr_mode = 0U;
    if(action == CAMERA_SETTINGS_RETURN_QR_CAMERA)
        return CAMERA_SETTINGS_EXIT_QR;
    if(action == CAMERA_SETTINGS_RETURN_REINIT)
        return CAMERA_SETTINGS_REINIT_PHOTO;
    return CAMERA_SETTINGS_EXIT_PHOTO;
}

camera_settings_exit_t camera_settings_handle_buttons(const hk_input_snapshot_t *input)
{
    if(!input)
        return CAMERA_SETTINGS_EXIT_NONE;
    if(settings_menu_handle_input(&g_camera_settings_menu, input) ==
       SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        return camera_settings_exit(input->state);
    return CAMERA_SETTINGS_EXIT_NONE;
}
