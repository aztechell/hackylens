#include "camera_photo_capture.h"

#include <stddef.h>

#include "../../config/camera_config.h"
#include "../../services/camera_frame.h"
#include "../../services/camera_session.h"

camera_photo_begin_t camera_photo_capture_begin(void)
{
    if(!camera_service_capture_ready())
        return CAMERA_PHOTO_BEGIN_NOT_READY;
    if(!camera_service_frame_snapshot(CAMERA_FRAME_WAIT_MS, NULL, NULL, NULL, NULL))
        return CAMERA_PHOTO_BEGIN_NO_FRAME;

    camera_service_freeze(1U);
    return CAMERA_PHOTO_BEGIN_READY;
}

void camera_photo_capture_end(void)
{
    camera_service_frame_snapshot_done(1U);
    camera_service_freeze(0U);
}
