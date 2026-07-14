#include "photo_service.h"

#include <stddef.h>

#include "camera_frame.h"
#include "camera_photo.h"
#include "../storage/photo_writer.h"

static uint16_t photo_service_read_pixel(void *ctx, uint32_t x, uint32_t y)
{
    (void)ctx;
    return camera_service_frame_pixel(x, y);
}

uint8_t photo_service_save_current_frame(char *saved_name, size_t saved_name_size)
{
    photo_source_t source;

    camera_service_photo_info(&source.format, &source.width, &source.height);
    source.read_pixel = photo_service_read_pixel;
    source.ctx = NULL;

    return photo_save_frame(&source, saved_name, saved_name_size);
}
