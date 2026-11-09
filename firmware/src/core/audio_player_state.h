#ifndef HK_AUDIO_PLAYER_STATE_H
#define HK_AUDIO_PLAYER_STATE_H

#include <stdint.h>

typedef struct
{
    const char *name;
    const char *status;
    uint32_t position;
    uint32_t duration;
    uint8_t playing;
} audio_player_view_t;

#endif
