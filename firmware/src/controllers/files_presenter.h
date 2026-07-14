#ifndef FILES_PRESENTER_H
#define FILES_PRESENTER_H

#include <stdint.h>

#include "../storage/file_result.h"
#include "../storage/image_decode.h"

void files_presenter_enter(void);
void files_presenter_show_status(const char *line);
void files_presenter_show_result(file_result_t result);
void files_presenter_render_list(void);
void files_presenter_render_preview(void);
void files_presenter_draw_row(uint8_t row);
void files_presenter_draw_delete_confirm(const char *name);
file_result_t files_presenter_render_image(const fat_file_entry_t *entry);

#endif
