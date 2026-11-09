#ifndef HK_AUDIO_RECORDINGS_H
#define HK_AUDIO_RECORDINGS_H

#include "../storage/fat_file_entry.h"

typedef struct
{
    const char *status;
    uint32_t seconds;
    uint8_t active;
    uint8_t stopping;
    uint8_t preparing;
} audio_recording_view_t;

const char *audio_recordings_load(void);
uint8_t audio_recordings_count(void);
const fat_file_entry_t *audio_recordings_get(uint8_t index);
void audio_recording_begin(void);
void audio_recording_end(void);
void audio_recording_close(uint8_t media_lost);
void audio_recording_tick(void);
audio_recording_view_t audio_recording_view(void);

#endif
