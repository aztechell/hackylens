#ifndef CAMERA_PHOTO_ENCODE_H
#define CAMERA_PHOTO_ENCODE_H

#include "camera_photo_format.h"

typedef uint16_t (*photo_pixel_reader_t)(void *ctx, uint32_t x, uint32_t y);

typedef struct
{
    photo_format_t format;
    uint16_t width;
    uint16_t height;
    photo_pixel_reader_t read_pixel;
    void *ctx;
} photo_source_t;

typedef struct
{
    const photo_source_t *source;
    uint32_t row_bytes;
    uint8_t header[PHOTO_BMP565_HEADER_SIZE];
    uint16_t header_size;
    uint8_t bytes_per_pixel;
    uint8_t bottom_up;
} photo_encoder_t;

uint32_t photo_encode_begin(photo_encoder_t *encoder, const photo_source_t *source);
void photo_encode_fill_file_bytes(const photo_encoder_t *encoder, uint32_t offset,
                                  uint8_t *dst, uint16_t length);

#endif
