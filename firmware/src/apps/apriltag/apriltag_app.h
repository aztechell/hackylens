#ifndef HK_APRILTAG_APP_H
#define HK_APRILTAG_APP_H

#include "../../core/hk_app.h"

void apriltag_enter(const hk_input_snapshot_t *input);
void apriltag_exit(void);
void apriltag_tick(const hk_input_snapshot_t *input);
void apriltag_handle_buttons(const hk_input_snapshot_t *input);
void apriltag_background_tick(const hk_input_snapshot_t *input);
uint8_t apriltag_handle_debug_command(const char *cmd);
void apriltag_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
extern const hk_legacy_app_entry_t apriltag_legacy_entry;

#endif
