#ifndef MINIMAL_PRIVATE_H
#define MINIMAL_PRIVATE_H

#include <hackylens/app.h>

typedef struct
{
    hk_owner_t owner;
    hk_time_t time;
    hk_input_t input;
    hk_display_t display;
    hk_app_service_t service;
    hk_deadline_t stop_deadline;
    uint32_t input_events;
    uint32_t media_events;
    uint32_t close_events;
    uint32_t ticks;
    uint32_t renders;
} minimal_state_t;

extern const hk_app_v2_entry_t minimal_app_entry;

#endif
