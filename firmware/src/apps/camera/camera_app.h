#ifndef HK_CAMERA_APP_H
#define HK_CAMERA_APP_H
#include <hackylens/app.h>
#include "../../core/hk_app.h"
uint8_t camera_handle_debug_command(const char *cmd);
void camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
extern const hk_app_v2_entry_t camera_v2_entry;
#endif
