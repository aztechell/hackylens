#ifndef IMAGE_DECODE_H
#define IMAGE_DECODE_H

#include "fat_file_entry.h"
#include "file_result.h"

#include "../config/camera_config.h"

#include "fat32_file.h"
#include "fat32_stream.h"
#include "photo_format.h"

typedef struct
{
    void (*begin)(void *context);
    void (*render_row_span)(void *context, uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order);
    void *context;
} file_image_sink_t;

uint8_t *image_decode_row_buffer(void);
uint32_t image_decode_row_buffer_size(void);

file_result_t files_open_png(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_bmp(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_raw565(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_ppm(const fat_file_entry_t *entry, const file_image_sink_t *sink);

#endif
