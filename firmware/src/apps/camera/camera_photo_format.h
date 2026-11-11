#ifndef PHOTO_FORMAT_H
#define PHOTO_FORMAT_H

#include <stdint.h>

#include "../../core/photo_types.h"

#include "camera_config.h"


const char *photo_format_label(photo_format_t format);
const char *photo_format_ext(photo_format_t format);
uint16_t photo_make_bmp_header(uint8_t *header, uint16_t width, uint16_t height,
                               uint8_t bytes_per_pixel);
uint16_t photo_make_ppm_header(uint8_t header[PHOTO_PPM_HEADER_MAX], uint16_t width, uint16_t height);

#endif
