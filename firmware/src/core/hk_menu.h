#ifndef HK_MENU_H
#define HK_MENU_H

#include "hk_app.h"

typedef struct
{
    void (*draw_chrome)(const char *title);
    void (*draw_title)(const char *title);
    void (*draw_item_at)(uint8_t index, const hk_app_t *app, uint8_t selected);
} hk_menu_view_t;

void menu_view_set(const hk_menu_view_t *view);
uint8_t hk_menu_index_get(void);
void menu_render(void);
void shell_show_menu(void);
uint8_t shell_open_app(const hk_app_t *app, const hk_input_snapshot_t *input);
void shell_open_selected(const hk_input_snapshot_t *input);
void menu_select_delta(int8_t delta);
void menu_select_vertical(void);
void menu_repeat_reset(void);
void menu_repeat_start(uint32_t button);
void menu_tick(const hk_input_snapshot_t *input);

#endif
