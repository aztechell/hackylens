#include "hk_menu.h"

#include <stdio.h>
#include <string.h>

#include "hk_app.h"

#include "../config/input_config.h"
#include "../config/menu_layout.h"

#include "hk_app_registry.h"
#include "hk_back_exit.h"
#include "hk_screen.h"

static uint8_t s_menu_index;
static uint32_t s_menu_repeat_button;
static uint8_t s_menu_repeat_ticks;
static hk_menu_view_t s_menu_view;

void menu_view_set(const hk_menu_view_t *view)
{
    if(view)
        s_menu_view = *view;
    else
        memset(&s_menu_view, 0, sizeof(s_menu_view));
}

static void menu_draw_chrome_if_ready(const char *title)
{
    if(s_menu_view.draw_chrome)
        s_menu_view.draw_chrome(title);
}

static void menu_draw_title_if_ready(const char *title)
{
    if(s_menu_view.draw_title)
        s_menu_view.draw_title(title);
}

static void menu_draw_registry_item(uint8_t index)
{
    if(s_menu_view.draw_item_at)
        s_menu_view.draw_item_at(index, &g_menu_items[index], (uint8_t)(index == s_menu_index));
}

uint8_t hk_menu_index_get(void)
{
    return s_menu_index;
}

void menu_render(void)
{
    hk_back_exit_set_armed(0);
    menu_draw_chrome_if_ready(g_menu_items[s_menu_index].title);
    for(uint8_t index = 0; index < MENU_ITEM_COUNT; index++)
        menu_draw_registry_item(index);
}

void shell_show_menu(void)
{
    const hk_app_t *app = hk_app_for_screen(hk_screen_get());

    if(app && app->exit)
        app->exit();
    hk_screen_set(SCREEN_MENU);
    hk_back_exit_set_armed(0);
    menu_render();
    activity_note();
    printf("[SHELL] screen MENU item=%s\r\n", g_menu_items[s_menu_index].title);
}

uint8_t shell_open_app(const hk_app_t *app, const hk_input_snapshot_t *input)
{
    uint8_t index;

    if(!app || !app->enter)
        return 0U;
    for(index = 0U; index < g_menu_item_count; index++)
    {
        if(&g_menu_items[index] == app)
            break;
    }
    if(index >= g_menu_item_count)
        return 0U;
    s_menu_index = index;
    s_menu_repeat_button = 0;
    s_menu_repeat_ticks = 0;
    printf("[MENU] open %s\r\n", app->title);
    app->enter(input);
    return 1U;
}

void shell_open_selected(const hk_input_snapshot_t *input)
{
    (void)shell_open_app(&g_menu_items[s_menu_index], input);
}

void menu_select_delta(int8_t delta)
{
    uint8_t previous = s_menu_index;

    if(delta < 0)
        s_menu_index = s_menu_index == 0 ? MENU_ITEM_COUNT - 1 : s_menu_index - 1;
    else if(delta > 0)
        s_menu_index = (uint8_t)((s_menu_index + 1) % MENU_ITEM_COUNT);

    if(previous != s_menu_index)
    {
        menu_draw_title_if_ready(g_menu_items[s_menu_index].title);
        menu_draw_registry_item(previous);
        menu_draw_registry_item(s_menu_index);
    }
    printf("[MENU] select %s\r\n", g_menu_items[s_menu_index].title);
}

void menu_select_vertical(void)
{
    uint8_t previous = s_menu_index;
    s_menu_index = (uint8_t)((s_menu_index + MENU_GRID_COLS) % MENU_ITEM_COUNT);
    if(previous != s_menu_index)
    {
        menu_draw_title_if_ready(g_menu_items[s_menu_index].title);
        menu_draw_registry_item(previous);
        menu_draw_registry_item(s_menu_index);
    }
    printf("[MENU] select %s\r\n", g_menu_items[s_menu_index].title);
}

void menu_repeat_reset(void)
{
    s_menu_repeat_button = 0;
    s_menu_repeat_ticks = 0;
}

void menu_repeat_start(uint32_t button)
{
    s_menu_repeat_button = button;
    s_menu_repeat_ticks = MENU_REPEAT_INITIAL_TICKS;
}

void menu_tick(const hk_input_snapshot_t *input)
{
    uint32_t buttons = input->state;

    if(hk_screen_get() != SCREEN_MENU)
    {
        menu_repeat_reset();
        return;
    }

    if(s_menu_repeat_button == 0 || !(buttons & s_menu_repeat_button))
    {
        if(buttons & BUTTON_LEFT)
            menu_repeat_start(BUTTON_LEFT);
        else if(buttons & BUTTON_RIGHT)
            menu_repeat_start(BUTTON_RIGHT);
        else
            menu_repeat_reset();
        return;
    }

    if(s_menu_repeat_ticks > 0)
    {
        s_menu_repeat_ticks--;
        return;
    }

    if(s_menu_repeat_button == BUTTON_LEFT)
    {
        menu_select_delta(-1);
    }
    else if(s_menu_repeat_button == BUTTON_RIGHT)
    {
        menu_select_delta(1);
    }
    s_menu_repeat_ticks = MENU_REPEAT_NEXT_TICKS;
}
