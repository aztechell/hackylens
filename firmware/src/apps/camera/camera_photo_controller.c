#include "camera_photo_controller.h"

#include <stdio.h>

#include "../../services/camera_frame.h"
#include "../../services/camera_persist_settings.h"

#include "camera_config.h"
#include "../../hal/hal_time.h"
#include "camera_photo_service.h"
#include "../../storage/file_mount.h"
#include "../../storage/file_write_error.h"
#include "../../ui/camera_status_view.h"
#include "../../ui/camera_view.h"
#include "camera_photo_format.h"
#include "camera_photo_capture.h"

void camera_photo_controller_take(camera_photo_preview_redraw_t redraw_preview)
{
    char saved_name[16];
    char status[24];
    camera_photo_begin_t photo_begin = camera_photo_capture_begin(status, sizeof(status));

    if(photo_begin == CAMERA_PHOTO_BEGIN_NOT_READY)
        return;

    if(photo_begin == CAMERA_PHOTO_BEGIN_NO_FRAME)
    {
        printf("[PHOTO] save fail no frame\r\n");
        camera_status_view_draw("SAVE FAIL", "NO FRAME");
        hal_sleep_ms(550);
        camera_view_clear();
        return;
    }

    {
        uint16_t width;
        uint16_t height;
        camera_service_frame_info(&width, &height);
        snprintf(status,
                 sizeof(status),
                 "%s %ux%u",
                 photo_format_label(camera_service_photo_format()),
                 width,
                 height);
    }
    camera_status_view_draw("SAVING...", status);

    if(!file_mount_if_needed())
    {
        printf("[PHOTO] save fail no sd\r\n");
        camera_status_view_draw("NO SD", "SAVE FAIL");
        hal_sleep_ms(750);
    }
    else if(photo_service_save_current_frame(saved_name, sizeof(saved_name)))
    {
        camera_status_view_draw("PHOTO SAVED", saved_name);
        if(camera_service_review_after_shot())
        {
            hal_sleep_ms(450);
            if(redraw_preview)
                redraw_preview();
            hal_sleep_ms(CAMERA_REVIEW_MS);
        }
        else
        {
            hal_sleep_ms(650);
        }
    }
    else
    {
        const char *error = file_write_last_error() ? file_write_last_error() : "UNKNOWN";
        printf("[PHOTO] save fail %s\r\n", error);
        camera_status_view_draw("SAVE FAIL", error);
        hal_sleep_ms(750);
    }

    camera_photo_capture_end();
    camera_view_clear();
}
