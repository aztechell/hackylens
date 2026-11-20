#ifndef HK_SETTINGS_APP_DATA_H
#define HK_SETTINGS_APP_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "../config/settings_config.h"
#include "../core/hk_menu_order.h"

void settings_app_data_read(uint8_t data[SETTINGS_APP_DATA_SIZE]);
void settings_app_data_write(const uint8_t data[SETTINGS_APP_DATA_SIZE]);
void settings_tag_distance_read(uint8_t data[SETTINGS_TAG_DISTANCE_BYTES]);
void settings_tag_distance_write(const uint8_t data[SETTINGS_TAG_DISTANCE_BYTES]);
uint32_t settings_object_disabled_classes(void);
void settings_set_object_disabled_classes(uint32_t mask);
const hk_menu_order_t *settings_menu_order(void);
uint8_t settings_set_menu_order(const hk_menu_order_t *order);

#endif
