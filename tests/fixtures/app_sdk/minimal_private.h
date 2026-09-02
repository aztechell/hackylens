#ifndef MINIMAL_PRIVATE_H
#define MINIMAL_PRIVATE_H

#include <hackylens/app.h>

typedef struct
{
    hk_owner_t owner;
    hk_time_t time;
    hk_input_t input;
    hk_input_t input_second;
    hk_display_t display;
    hk_app_service_t service;
    hk_deadline_t stop_deadline;
    uint32_t input_events;
    uint32_t media_events;
    uint32_t close_events;
    uint32_t ticks;
    uint32_t renders;
    uint8_t consume_input;
} minimal_state_t;

extern const hk_app_v2_entry_t minimal_app_entry;
void minimal_app_set_consume_input(uint8_t consume);
int minimal_app_check_input_overflow(uint32_t expected_dropped);
int minimal_app_check_time_contract(void);
int minimal_app_check_display_contract(void);
int minimal_app_check_stale_reacquire(void);

#endif
