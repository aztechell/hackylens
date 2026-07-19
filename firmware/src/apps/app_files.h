#ifndef APP_FILES_H
#define APP_FILES_H

#include "../core/hk_app.h"

void files_enter(const hk_input_snapshot_t *input);
void files_exit(void);
void files_tick(const hk_input_snapshot_t *input);
void files_handle_buttons(const hk_input_snapshot_t *input);

#endif
