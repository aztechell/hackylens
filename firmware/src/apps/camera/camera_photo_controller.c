#include "camera_photo_controller.h"

#include <stdio.h>

#include <hackylens/capability/time.h>

#include "../../services/camera_frame.h"
#include "../../services/camera_persist_settings.h"
#include "../../storage/file_write_error.h"
#include "../../ui/camera_status_view.h"
#include "../../ui/camera_view.h"
#include "camera_config.h"
#include "camera_photo_capture.h"
#include "camera_photo_format.h"
#include "camera_photo_service.h"

static hk_deadline_t s_until;
static uint8_t s_snapshot;
static uint8_t s_review_pending;

static void photo_status_for(uint32_t duration_ms)
{
    if(hk_time_deadline_after_us(hk_time_service(), duration_ms * 1000ULL, &s_until) != HK_OK)
        camera_photo_controller_dismiss();
}

uint8_t camera_photo_controller_active(void)
{
    return s_until.at_us != 0U;
}

void camera_photo_controller_dismiss(void)
{
    s_until = HK_DEADLINE_IMMEDIATE;
    s_review_pending = 0U;
    if(s_snapshot)
    {
        s_snapshot = 0U;
        camera_photo_capture_end();
    }
    camera_view_clear();
}

void camera_photo_controller_tick(camera_photo_preview_redraw_t redraw_preview)
{
    uint64_t now;
    if(hk_time_now_us(hk_time_service(), &now) != HK_OK)
    {
        camera_photo_controller_dismiss();
        return;
    }
    if(now < s_until.at_us)
        return;
    if(s_review_pending && redraw_preview)
    {
        s_review_pending = 0U;
        redraw_preview();
        photo_status_for(CAMERA_REVIEW_MS);
        return;
    }
    camera_photo_controller_dismiss();
}

void camera_photo_controller_take(void)
{
    char saved_name[16];
    char status[24];
    camera_photo_begin_t photo_begin = camera_photo_capture_begin();

    if(photo_begin == CAMERA_PHOTO_BEGIN_NOT_READY)
        return;
    if(photo_begin == CAMERA_PHOTO_BEGIN_NO_FRAME)
    {
        printf("[PHOTO] save fail no frame\r\n");
        camera_status_view_draw("SAVE FAIL", "NO FRAME");
        photo_status_for(550U);
        return;
    }

    s_snapshot = 1U;
    uint16_t width;
    uint16_t height;
    camera_service_frame_info(&width, &height);
    snprintf(status, sizeof(status), "%s %ux%u",
             photo_format_label(camera_service_photo_format()), width, height);
    camera_status_view_draw("SAVING...", status);

    if(photo_service_save_current_frame(saved_name, sizeof(saved_name)))
    {
        printf("[PHOTO] saved %s\r\n", saved_name);
        camera_status_view_draw("PHOTO SAVED", saved_name);
        s_review_pending = camera_service_review_after_shot();
        photo_status_for(350U);
    }
    else
    {
        const char *error = file_write_last_error();
        if(!error)
            error = "UNKNOWN";
        printf("[PHOTO] save failed: %s\r\n", error);
        camera_status_view_draw("SAVE FAIL", error);
        photo_status_for(750U);
    }
}
