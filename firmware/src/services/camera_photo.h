#ifndef CAMERA_PHOTO_H
#define CAMERA_PHOTO_H

#include <stddef.h>
#include <stdint.h>

#include "../core/photo_types.h"

typedef enum
{
    CAMERA_PHOTO_BEGIN_READY = 0,
    CAMERA_PHOTO_BEGIN_NOT_READY,
    CAMERA_PHOTO_BEGIN_NO_FRAME,
} camera_photo_begin_t;

void camera_service_photo_info(photo_format_t *format, uint16_t *width, uint16_t *height);
camera_photo_begin_t camera_service_photo_begin(char *status, size_t status_size);
void camera_service_photo_end(void);

#endif
