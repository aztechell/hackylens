#ifndef HK_FACE_RECOGNITION_APP_H
#define HK_FACE_RECOGNITION_APP_H

#include <hackylens/app/runtime.h>

extern const hk_app_v2_entry_t face_recognition_v2_entry;
void face_recognition_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
uint8_t face_recognition_handle_debug_command(const char *cmd);

#endif
