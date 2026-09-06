#ifndef HK_FACE_DETECT_APP_H
#define HK_FACE_DETECT_APP_H
#include <hackylens/app.h>
#include "../../core/hk_app.h"
uint8_t face_detect_handle_debug_command(const char *cmd);
void face_detect_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
extern const hk_app_v2_entry_t face_detect_v2_entry;
#endif
