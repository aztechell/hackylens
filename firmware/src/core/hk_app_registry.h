#ifndef HK_APP_REGISTRY_H
#define HK_APP_REGISTRY_H

#include "hk_app.h"


extern const hk_app_t g_menu_items[];
extern const uint8_t g_menu_item_count;
#define MENU_ITEM_COUNT g_menu_item_count

const hk_app_t *hk_app_for_screen(screen_t screen);

#endif
