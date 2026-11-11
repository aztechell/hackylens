#ifndef HK_ACCELEROMETER_APP_H
#define HK_ACCELEROMETER_APP_H

#include <hackylens/app.h>

extern const hk_app_v2_entry_t accelerometer_v2_entry;
uint8_t accelerometer_handle_debug_command(const char *command);

#endif
