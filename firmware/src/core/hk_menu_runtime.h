#ifndef HK_MENU_RUNTIME_H
#define HK_MENU_RUNTIME_H

#include <hackylens/app/runtime.h>

#include "hk_menu.h"

typedef struct
{
    uint8_t (*enter)(const hk_app_t *app, const hk_input_snapshot_t *input);
    void (*exit)(const hk_app_t *app, hk_app_stop_reason_t reason);
} hk_menu_owner_hooks_t;

void menu_owner_hooks_set(const hk_menu_owner_hooks_t *hooks);
void shell_show_menu_reason(hk_app_stop_reason_t reason);

#endif
