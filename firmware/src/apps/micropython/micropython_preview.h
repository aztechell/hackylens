#ifndef HK_MICROPYTHON_PREVIEW_H
#define HK_MICROPYTHON_PREVIEW_H

#include "micropython_view.h"

userfs_result_t micropython_preview_open(micropython_view_state_t *ui,
                                         uint8_t columns);
userfs_result_t micropython_preview_move(micropython_view_state_t *ui,
                                         int8_t direction);

#endif
