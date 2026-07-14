#include "camera_settings_controller.h"

#include <stdio.h>

#include "../services/camera_light.h"
#include "../services/camera_session.h"
#include "../services/camera_persist_settings.h"
#include "../services/camera_settings_navigation.h"
#include "../services/camera_status.h"

#include "../core/hk_app.h"
#include "../core/hk_camera_sizes.h"
#include "../core/camera_types.h"
#include "../core/photo_types.h"

#include "../config/input_config.h"
#include "../config/settings_config.h"
#include "camera_settings_model.h"
#include "../core/hk_back_exit.h"
#include "../core/hk_screen.h"
#include "../services/settings_lights.h"
#include "../services/qr_service.h"
#include "../storage/photo_format.h"

static void camera_settings_enter_common(uint8_t qr_mode)
{
    hk_screen_set(SCREEN_CAMERA_SETTINGS);
    hk_back_exit_set_armed(0);
    camera_service_settings_begin(qr_mode);
    camera_settings_model_render();
    printf("[SHELL] screen %s SETTINGS\r\n", qr_mode ? "QR" : "CAMERA");
}

void camera_settings_enter(void)
{
    camera_settings_enter_common(0);
}

void qr_settings_enter(void)
{
    camera_service_freeze(1);
    camera_settings_enter_common(1);
}

static void camera_settings_select_delta(int8_t delta)
{
    uint8_t previous = camera_service_settings_index();
    uint8_t previous_top = camera_service_settings_top();
    uint8_t next = previous;
    uint8_t row_count = camera_settings_model_row_count();

    if(delta < 0)
        next = previous == 0 ? row_count - 1 : previous - 1;
    else if(delta > 0)
        next = (uint8_t)((previous + 1U) % row_count);

    camera_service_settings_set_index(next);
    camera_settings_model_ensure_visible();
    if(previous != camera_service_settings_index())
    {
        if(previous_top != camera_service_settings_top())
            camera_settings_model_render();
        else
        {
            camera_settings_model_draw_row(previous);
            camera_settings_model_draw_row(camera_service_settings_index());
        }
    }
}

static camera_settings_exit_t camera_settings_exit(uint32_t input_state)
{
    camera_settings_return_t action = camera_service_prepare_settings_return((input_state & BUTTON_OK) ? 1 : 0);

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

    if(input->pressed & BUTTON_BACK)
        return camera_settings_exit(input->state);

    if(input->pressed & BUTTON_LEFT)
        camera_settings_select_delta(-1);
    if(input->pressed & BUTTON_RIGHT)
        camera_settings_select_delta(1);
    if(input->pressed & BUTTON_OK)
    {
        uint8_t index = camera_service_settings_index();

        if(camera_service_settings_qr_mode())
        {
            if(index == QR_SETTINGS_RATE)
            {
                uint8_t rate = qr_service_cycle_decode_rate();
                camera_settings_model_draw_row(QR_SETTINGS_RATE);
                printf("[QR] decode_rate=%u/s\r\n", rate);
            }
            else if(index == QR_SETTINGS_FPS)
            {
                uint8_t enabled = camera_service_toggle_fps();
                camera_settings_model_draw_row(QR_SETTINGS_FPS);
                printf("[QR] fps_counter=%u\r\n", enabled);
            }
            else if(index == QR_SETTINGS_LIGHT)
            {
                camera_light_mode_t mode = camera_service_toggle_light_mode();
                camera_settings_model_draw_row(QR_SETTINGS_LIGHT);
                printf("[QR] light_mode=%s\r\n", camera_light_mode_label(mode));
            }
            else if(index == QR_SETTINGS_RGB_RED ||
                    index == QR_SETTINGS_RGB_GREEN ||
                    index == QR_SETTINGS_RGB_BLUE)
            {
                camera_rgb_channel_t channel = index == QR_SETTINGS_RGB_RED ? CAMERA_RGB_RED :
                                               index == QR_SETTINGS_RGB_GREEN ? CAMERA_RGB_GREEN :
                                               CAMERA_RGB_BLUE;
                camera_service_cycle_rgb_channel(channel);
                camera_settings_model_draw_row(index);
                printf("[QR] rgb=%u/%u/%u\r\n",
                       camera_service_rgb_channel(CAMERA_RGB_RED),
                       camera_service_rgb_channel(CAMERA_RGB_GREEN),
                       camera_service_rgb_channel(CAMERA_RGB_BLUE));
            }
            return CAMERA_SETTINGS_EXIT_NONE;
        }

        if(index == CAMERA_SETTINGS_REVIEW)
        {
            uint8_t review = camera_service_toggle_review_after_shot();
            camera_settings_model_draw_row(CAMERA_SETTINGS_REVIEW);
            printf("[CAM] review_after_shot=%u\r\n", review);
        }
        else if(index == CAMERA_SETTINGS_FPS)
        {
            uint8_t enabled = camera_service_toggle_fps();
            camera_settings_model_draw_row(CAMERA_SETTINGS_FPS);
            printf("[CAM] fps_counter=%u\r\n", enabled);
        }
        else if(index == CAMERA_SETTINGS_LIGHT)
        {
            camera_light_mode_t mode = camera_service_toggle_light_mode();
            camera_settings_model_draw_row(CAMERA_SETTINGS_LIGHT);
            printf("[CAM] light_mode=%s\r\n", camera_light_mode_label(mode));
        }
        else if(index == CAMERA_SETTINGS_RGB_RED ||
                index == CAMERA_SETTINGS_RGB_GREEN ||
                index == CAMERA_SETTINGS_RGB_BLUE)
        {
            camera_rgb_channel_t channel = index == CAMERA_SETTINGS_RGB_RED ? CAMERA_RGB_RED :
                                           index == CAMERA_SETTINGS_RGB_GREEN ? CAMERA_RGB_GREEN :
                                           CAMERA_RGB_BLUE;
            camera_service_cycle_rgb_channel(channel);
            camera_settings_model_draw_row(index);
            printf("[CAM] rgb=%u/%u/%u\r\n",
                   camera_service_rgb_channel(CAMERA_RGB_RED),
                   camera_service_rgb_channel(CAMERA_RGB_GREEN),
                   camera_service_rgb_channel(CAMERA_RGB_BLUE));
        }
        else if(index == CAMERA_SETTINGS_FORMAT)
        {
            photo_format_t format = camera_service_cycle_photo_format();
            camera_settings_model_draw_row(CAMERA_SETTINGS_FORMAT);
            printf("[CAM] photo_format=%s\r\n", photo_format_label(format));
        }
        else if(index == CAMERA_SETTINGS_SIZE)
        {
            camera_size_t size = camera_service_cycle_size();
            camera_settings_model_draw_row(CAMERA_SETTINGS_SIZE);
            printf("%s size_setting=%s\r\n", camera_log_prefix(), camera_size_label(size));
        }
    }

    return CAMERA_SETTINGS_EXIT_NONE;
}
