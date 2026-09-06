#ifndef HK_APP_REGISTRY_H
#define HK_APP_REGISTRY_H

#include "hk_app.h"

extern const hk_app_t *const g_menu_items[];
extern const uint8_t g_menu_item_count;
#define MENU_ITEM_COUNT g_menu_item_count

const hk_app_t *hk_app_for_id(const char *id);
const hk_app_t *hk_app_for_autostart_id(hk_autostart_id_t id);
uint8_t hk_app_autostart_id_is_persistable(hk_autostart_id_t id);
uint8_t hk_app_autostart_count(void);
const hk_app_t *hk_app_autostart_at(uint8_t index);
uint8_t hk_app_registry_handle_debug_command(const char *cmd);

#endif
