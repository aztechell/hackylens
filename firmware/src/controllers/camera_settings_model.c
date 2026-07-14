#include "camera_settings_model.h"

#include <stdio.h>

#include "../core/hk_camera_sizes.h"

#include "../services/camera_light.h"
#include "../services/camera_persist_settings.h"
#include "../services/camera_settings_navigation.h"

#include "../config/settings_config.h"
#include "../config/photo_storage_config.h"
#include "../config/camera_layout.h"
#include "hk_config.h"

#include "../services/settings_lights.h"
#include "../services/qr_service.h"
#include "../storage/photo_format.h"
#include "../ui/camera_settings_view.h"

static camera_settings_view_row_t g_camera_settings_rows[CAMERA_SETTINGS_ROW_COUNT];

uint8_t camera_settings_model_row_count(void)
{
    return camera_service_settings_qr_mode() ? QR_SETTINGS_ROW_COUNT : CAMERA_SETTINGS_ROW_COUNT;
}

static const char *camera_settings_row_title(uint8_t index)
{
    if(camera_service_settings_qr_mode())
    {
        if(index == QR_SETTINGS_RATE)
            return "Decode Rate";
        if(index == QR_SETTINGS_FPS)
            return "FPS Counter";
        if(index == QR_SETTINGS_LIGHT)
            return "Light";
        if(index == QR_SETTINGS_RGB_RED)
            return "RGB Red";
        if(index == QR_SETTINGS_RGB_GREEN)
            return "RGB Green";
        if(index == QR_SETTINGS_RGB_BLUE)
            return "RGB Blue";
        return "Version";
    }

    if(index == CAMERA_SETTINGS_REVIEW)
        return "Review";
    if(index == CAMERA_SETTINGS_FPS)
        return "FPS Counter";
    if(index == CAMERA_SETTINGS_LIGHT)
        return "Light";
    if(index == CAMERA_SETTINGS_RGB_RED)
        return "RGB Red";
    if(index == CAMERA_SETTINGS_RGB_GREEN)
        return "RGB Green";
    if(index == CAMERA_SETTINGS_RGB_BLUE)
        return "RGB Blue";
    if(index == CAMERA_SETTINGS_FOLDER)
        return "Folder";
    if(index == CAMERA_SETTINGS_FORMAT)
        return "Format";
    if(index == CAMERA_SETTINGS_SIZE)
        return "Size";
    return "Version";
}

static void camera_settings_row_value(uint8_t index, char *value, size_t value_size)
{
    if(camera_service_settings_qr_mode())
    {
        if(index == QR_SETTINGS_RATE)
            snprintf(value, value_size, "%u/s", qr_service_decode_rate());
        else if(index == QR_SETTINGS_FPS)
            snprintf(value, value_size, "%s", camera_service_fps_enabled() ? "ON" : "OFF");
        else if(index == QR_SETTINGS_LIGHT)
            snprintf(value, value_size, "%s", camera_light_mode_label(camera_service_light_mode()));
        else if(index == QR_SETTINGS_RGB_RED)
            snprintf(value, value_size, "%u", camera_service_rgb_channel(CAMERA_RGB_RED));
        else if(index == QR_SETTINGS_RGB_GREEN)
            snprintf(value, value_size, "%u", camera_service_rgb_channel(CAMERA_RGB_GREEN));
        else if(index == QR_SETTINGS_RGB_BLUE)
            snprintf(value, value_size, "%u", camera_service_rgb_channel(CAMERA_RGB_BLUE));
        else
            snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
        return;
    }

    if(index == CAMERA_SETTINGS_REVIEW)
        snprintf(value, value_size, "%s", camera_service_review_after_shot() ? "ON" : "OFF");
    else if(index == CAMERA_SETTINGS_FPS)
        snprintf(value, value_size, "%s", camera_service_fps_enabled() ? "ON" : "OFF");
    else if(index == CAMERA_SETTINGS_LIGHT)
        snprintf(value, value_size, "%s", camera_light_mode_label(camera_service_light_mode()));
    else if(index == CAMERA_SETTINGS_RGB_RED)
        snprintf(value, value_size, "%u", camera_service_rgb_channel(CAMERA_RGB_RED));
    else if(index == CAMERA_SETTINGS_RGB_GREEN)
        snprintf(value, value_size, "%u", camera_service_rgb_channel(CAMERA_RGB_GREEN));
    else if(index == CAMERA_SETTINGS_RGB_BLUE)
        snprintf(value, value_size, "%u", camera_service_rgb_channel(CAMERA_RGB_BLUE));
    else if(index == CAMERA_SETTINGS_FOLDER)
        snprintf(value, value_size, "%s", PHOTO_DIR_LONG_NAME);
    else if(index == CAMERA_SETTINGS_FORMAT)
        snprintf(value, value_size, "%s", photo_format_label(camera_service_photo_format()));
    else if(index == CAMERA_SETTINGS_SIZE)
        snprintf(value, value_size, "%s", camera_size_label(camera_service_size()));
    else
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
}

static void camera_settings_build_model(camera_settings_view_model_t *model)
{
    uint8_t row_count = camera_settings_model_row_count();

    for(uint8_t i = 0; i < row_count; i++)
    {
        g_camera_settings_rows[i].title = camera_settings_row_title(i);
        camera_settings_row_value(i, g_camera_settings_rows[i].value, sizeof(g_camera_settings_rows[i].value));
    }

    model->title = camera_service_settings_qr_mode() ? "QR SETTINGS" : "CAM SETTINGS";
    model->rows = g_camera_settings_rows;
    model->row_count = row_count;
    model->index = camera_service_settings_index();
    model->top = camera_service_settings_top();
}

void camera_settings_model_ensure_visible(void)
{
    uint8_t index = camera_service_settings_index();
    uint8_t top = camera_service_settings_top();
    uint8_t row_count;

    if(index < top)
        top = index;
    else if(index >= top + CAMERA_SETTINGS_VISIBLE_ROWS)
        top = (uint8_t)(index - CAMERA_SETTINGS_VISIBLE_ROWS + 1U);

    row_count = camera_settings_model_row_count();
    if(top + CAMERA_SETTINGS_VISIBLE_ROWS > row_count)
        top = row_count > CAMERA_SETTINGS_VISIBLE_ROWS ? row_count - CAMERA_SETTINGS_VISIBLE_ROWS : 0;

    camera_service_settings_set_top(top);
}

void camera_settings_model_draw_row(uint8_t index)
{
    camera_settings_view_model_t model;

    camera_settings_build_model(&model);
    camera_settings_view_draw_row(&model, index);
}

void camera_settings_model_render(void)
{
    camera_settings_view_model_t model;

    camera_settings_model_ensure_visible();
    camera_settings_build_model(&model);
    camera_settings_view_render(&model);
}
