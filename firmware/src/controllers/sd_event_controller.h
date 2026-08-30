#ifndef SD_EVENT_CONTROLLER_H
#define SD_EVENT_CONTROLLER_H

#include "../core/hk_events.h"

typedef uint8_t (*sd_event_app_hook_t)(hk_sd_event_t event);

void sd_event_controller_set_app_hook(sd_event_app_hook_t hook);
void sd_event_controller_handle(hk_sd_event_t event);

#endif
