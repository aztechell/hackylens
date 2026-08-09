#ifndef HK_MICROPYTHON_VIEW_H
#define HK_MICROPYTHON_VIEW_H

#include <stdint.h>

#include "../../services/micropython_runtime.h"
#include "../../storage/userfs.h"
#include "micropython_config.h"

void micropython_view_render(
    const micropython_runtime_status_t *runtime,
    const userfs_status_t *filesystem,
    const char *startup,
    const char logs[MICROPYTHON_LOG_LINES][MICROPYTHON_LOG_COLUMNS + 1U]);
void micropython_view_draw_icon(uint16_t x, uint16_t y,
                                uint16_t color, uint16_t background);

#endif
