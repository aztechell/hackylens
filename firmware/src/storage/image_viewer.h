#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "fat_file_entry.h"
#include "file_result.h"
#include "image_decode.h"


uint8_t files_entry_is_image(const fat_file_entry_t *entry);
file_result_t files_open_image_entry(const fat_file_entry_t *entry, const file_image_sink_t *sink);

#endif
