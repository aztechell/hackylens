#ifndef HK_AUDIO_PLAYER_H
#define HK_AUDIO_PLAYER_H

#include "audio_service.h"
#include "../core/audio_player_state.h"
#include "../storage/fat_file_entry.h"

/* Returns an error message or NULL when the player can be shown. */
const char *audio_player_open(hk_audio_owner_t owner, const fat_file_entry_t *entry);
void audio_player_toggle(hk_audio_owner_t owner);
void audio_player_close(hk_audio_owner_t owner);
audio_player_view_t audio_player_view(hk_audio_owner_t owner);
void audio_player_tick(void);
uint8_t audio_player_active(void);
const char *audio_volume_adjust(int8_t steps);

#endif
