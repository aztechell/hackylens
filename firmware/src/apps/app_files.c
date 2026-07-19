#include "app_files.h"

#include "../core/hk_app.h"
#include "../controllers/files_controller.h"

void files_enter(const hk_input_snapshot_t *input)
{
    files_controller_enter(input);
}

void files_exit(void)
{
    files_controller_exit();
}

void files_tick(const hk_input_snapshot_t *input)
{
    files_controller_tick(input);
}

void files_handle_buttons(const hk_input_snapshot_t *input)
{
    files_controller_handle_buttons(input);
}
