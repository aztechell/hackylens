#ifndef HACKYLENS_APP_RECORDER_H
#define HACKYLENS_APP_RECORDER_H

#include <hackylens/app.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The recorder service is scoped to the built-in RECORDER app. Strings
 * returned by this interface are borrowed for the current callback. */
typedef enum
{
    HK_RECORDER_START,
    HK_RECORDER_SAVE,
    HK_RECORDER_LIST,
    HK_RECORDER_OPEN,
    HK_RECORDER_PLAY_STOP,
    HK_RECORDER_CLOSE_PLAYER,
    HK_RECORDER_ADJUST_VOLUME,
    HK_RECORDER_MEDIA_REMOVED,
} hk_recorder_action_t;

typedef struct
{
    const char *record_status;
    const char *player_name;
    const char *player_status;
    uint32_t recorded_seconds;
    uint32_t player_position;
    uint32_t player_duration;
    uint8_t recording;
    uint8_t saving;
    uint8_t preparing;
    uint8_t playing;
    uint8_t count;
    uint8_t volume;
} hk_recorder_info_t;

hk_result_t hk_recorder_info(const hk_app_context_t *ctx, hk_recorder_info_t *info);
const char *hk_recorder_name(const hk_app_context_t *ctx, uint8_t index);
const char *hk_recorder_action(const hk_app_context_t *ctx, hk_recorder_action_t action,
                               int16_t value);
hk_result_t hk_recorder_retire(const hk_app_context_t *ctx, hk_deadline_t deadline);
hk_result_t hk_recorder_player_redraw(const hk_app_context_t *ctx, uint16_t width, uint8_t full);
hk_result_t hk_recorder_player_render(const hk_app_context_t *ctx, hk_app_surface_t *surface,
                                      const char *message, uint8_t full);

#ifdef __cplusplus
}
#endif

#endif
