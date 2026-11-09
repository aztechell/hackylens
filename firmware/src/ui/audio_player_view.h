#ifndef HK_UI_AUDIO_PLAYER_VIEW_H
#define HK_UI_AUDIO_PLAYER_VIEW_H

#include <hackylens/app.h>

#include "../core/audio_player_state.h"

hk_result_t audio_player_redraw(const hk_app_context_t *ctx, uint16_t width, uint8_t full);
hk_result_t audio_player_draw(hk_app_surface_t *surface, const audio_player_view_t *player,
                              uint8_t volume, const char *message, uint8_t full);

#endif
