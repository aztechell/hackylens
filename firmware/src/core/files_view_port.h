#ifndef HK_FILES_VIEW_PORT_H
#define HK_FILES_VIEW_PORT_H

#include <stdint.h>

#include "file_list_item.h"

typedef struct
{
    void (*enter)(void);
    void (*draw_status)(const char *line);
    void (*draw_row)(uint8_t row, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index);
    void (*render_list)(uint8_t sd_present, uint8_t fat_mounted, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index);
    void (*render_preview)(const char *preview, uint16_t len, uint8_t page);
    void (*clear_image)(void);
    void (*render_image_row_span)(uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order);
    void (*draw_delete_confirm)(const char *name);
} files_view_ops_t;

void files_view_register(const files_view_ops_t *ops);

#endif
