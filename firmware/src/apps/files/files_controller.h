#ifndef FILES_CONTROLLER_H
#define FILES_CONTROLLER_H

#include <hackylens/app.h>

typedef struct
{
    const hk_input_t *input;
    const hk_time_t *time;
    uint8_t close_requested;
    uint8_t player_drawn;
    uint8_t player_volume;
    uint16_t player_width;
    uint32_t player_position;
    const char *player_status;
    const char *player_error;
} files_state_t;

void files_controller_reset(files_state_t *state);
void files_controller_enter(files_state_t *state);
void files_controller_exit(files_state_t *state);
void files_controller_handle_input(
    files_state_t *state, const hk_input_event_t *event);
void files_controller_tick(files_state_t *state, uint32_t buttons);
void files_controller_poll_animation(files_state_t *state);
const char *files_controller_audio_error(void);
void files_controller_handle_media(
    files_state_t *state, hk_app_media_kind_t kind);

#endif
