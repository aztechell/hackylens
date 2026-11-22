#ifndef HK_CAMERA_DATA_SAVE_H
#define HK_CAMERA_DATA_SAVE_H

#include "script_sd_store.h"

script_sd_result_t camera_data_save_begin(const void *owner, uint32_t lease,
                                          const char *name, size_t length);
script_sd_result_t camera_data_save_step(const void *owner);
void camera_data_save_cancel(const void *owner);
uint8_t camera_data_save_holds(const void *owner, uint32_t lease);

#endif
