#include "face_detect_controller.h"

#include <stdio.h>

#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../services/camera_photo.h"
#include "../../ui/camera_status_view.h"
#include "face_detect_detector.h"
#include "face_detect_view.h"

static uint8_t g_error;

void face_detect_controller_enter(const hk_input_snapshot_t *input)
{
    face_detect_load_result_t result = face_detect_detector_load();
    g_error = result != FACE_DETECT_LOAD_OK;
    if(g_error)
    {
        hk_screen_set(SCREEN_FACE_DETECT);
        camera_status_view_draw("FACE ERROR", face_detect_detector_error_label(result));
        printf("[FACE] load %s\r\n", face_detect_detector_error_label(result));
        return;
    }
    camera_runtime_enter(CAMERA_RUNTIME_FACE_DETECT, input);
    face_detect_detector_attach_camera();
}

void face_detect_controller_exit(void)
{
    face_detect_detector_unload();
}

void face_detect_controller_tick(const hk_input_snapshot_t *input)
{
    const face_detect_box_t *boxes;
    uint16_t width;
    uint16_t height;
    uint8_t count;

    if(g_error || !camera_runtime_tick(input))
        return;
    face_detect_detector_process_frame();
    boxes = face_detect_detector_boxes(&count);
    camera_service_photo_info(NULL, &width, &height);
    face_detect_view_draw_boxes(width, height, boxes, count);
}

void face_detect_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(g_error)
    {
        if(input && (input->pressed & BUTTON_BACK))
            shell_show_menu();
        return;
    }
    (void)camera_runtime_handle_input(input);
}
