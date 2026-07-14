#include "face_detect_controller.h"

#include <stdio.h>

#include "../config/display_config.h"
#include "../config/input_config.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../services/camera_photo.h"
#include "../services/face_detector.h"
#include "../ui/camera_status_view.h"
#include "../ui/camera_view.h"
#include "camera_runtime_controller.h"

static uint8_t g_error;

void face_detect_controller_enter(const hk_input_snapshot_t *input)
{
    face_detect_load_result_t result = face_detector_load();
    g_error = result != FACE_DETECT_LOAD_OK;
    if(g_error)
    {
        hk_screen_set(SCREEN_FACE_DETECT);
        camera_status_view_draw("FACE ERROR", face_detector_error_label(result));
        printf("[FACE] load %s\r\n", face_detector_error_label(result));
        return;
    }
    camera_runtime_enter(CAMERA_RUNTIME_FACE_DETECT, input);
    face_detector_attach_camera();
}

void face_detect_controller_tick(const hk_input_snapshot_t *input)
{
    camera_view_frame_t view;
    const face_detect_box_t *boxes;
    camera_view_rect_t rects[FACE_DETECT_BOX_MAX];
    uint8_t count;

    if(g_error || !camera_runtime_tick(input))
        return;
    face_detector_process_frame();
    boxes = face_detector_boxes(&count);
    for(uint8_t i = 0; i < count; i++)
    {
        rects[i].x = boxes[i].x; rects[i].y = boxes[i].y;
        rects[i].w = boxes[i].w; rects[i].h = boxes[i].h;
    }
    view.pixels = NULL;
    camera_service_photo_info(NULL, &view.width, &view.height);
    view.rotate = 0;
    view.fps_overlay = 0;
    view.light_overlay = 0;
    view.fps_text[0] = '\0';
    view.light_text[0] = '\0';
    camera_view_draw_rects(&view, rects, count, COLOR_TERM_GREEN);
}

void face_detect_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(g_error)
    {
        if(input && (input->pressed & BUTTON_BACK))
        {
            face_detector_unload();
            shell_show_menu();
        }
        return;
    }
    if(camera_runtime_handle_input(input) == CAMERA_RUNTIME_INPUT_EXIT)
        face_detector_unload();
}
