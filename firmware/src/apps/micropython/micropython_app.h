#ifndef HK_MICROPYTHON_APP_H
#define HK_MICROPYTHON_APP_H

#include "micropython_config.h"
#include <hackylens/app.h>

void micropython_enter(const hk_input_snapshot_t *input);
void micropython_exit(void);
void micropython_tick(const hk_input_snapshot_t *input);
void micropython_handle_buttons(const hk_input_snapshot_t *input);
void micropython_poll_output(const hk_input_snapshot_t *input);
void micropython_draw_icon(uint16_t x, uint16_t y,
                           uint16_t color, uint16_t background);
uint8_t micropython_handle_debug_command(const char *command);
extern const hk_app_v2_entry_t micropython_v2_entry;

#endif
