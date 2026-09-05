#ifndef FILES_CONTROLLER_H
#define FILES_CONTROLLER_H

#include <hackylens/app.h>

typedef struct
{
    hk_owner_t owner;
    hk_input_t input;
    hk_time_t time;
    uint8_t close_requested;
} files_state_t;

void files_controller_reset(files_state_t *state);
void files_controller_enter(files_state_t *state);
void files_controller_exit(files_state_t *state);
void files_controller_handle_input(
    files_state_t *state, const hk_input_event_t *event);
void files_controller_tick(files_state_t *state, uint32_t buttons);
void files_controller_poll_animation(files_state_t *state);
void files_controller_handle_media(
    files_state_t *state, hk_app_media_kind_t kind);

#endif
