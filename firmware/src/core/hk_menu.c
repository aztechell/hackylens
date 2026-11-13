#include "hk_menu_runtime.h"

#include <stdio.h>
#include <string.h>

#include "hk_app.h"

#include "../config/input_config.h"
#include "../config/menu_layout.h"

#include "hk_app_registry.h"
#include "hk_back_exit.h"
#include "hk_screen.h"

static uint8_t s_menu_index;
static uint8_t s_rendered_count;
static const hk_app_t *s_active_app;
static uint32_t s_menu_repeat_button;
static uint8_t s_menu_repeat_ticks;
static hk_menu_view_t s_menu_view;
static hk_menu_owner_hooks_t s_owner_hooks;

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

static uint8_t menu_item_count(void)
{
    return MENU_ITEM_COUNT + (s_owner_hooks.shortcut_count ?
           s_owner_hooks.shortcut_count() : 0U);
}

static const char *menu_shortcut_name(uint8_t index)
{
    return s_owner_hooks.shortcut_name ?
           s_owner_hooks.shortcut_name(index - MENU_ITEM_COUNT) : NULL;
}

static const char *menu_item_title(uint8_t index)
{
    if(index < MENU_ITEM_COUNT)
        return g_menu_items[index]->title;
    const char *name = menu_shortcut_name(index);
    return name ? name : "NO APPS";
}

static void menu_draw_item(uint8_t index)
{
    if(index / MENU_PAGE_ITEMS != s_menu_index / MENU_PAGE_ITEMS)
        return;
    uint8_t slot = index % MENU_PAGE_ITEMS;
    uint8_t selected = index == s_menu_index;
    if(index < MENU_ITEM_COUNT)
    {
        if(s_menu_view.draw_item_at)
            s_menu_view.draw_item_at(slot, g_menu_items[index], selected);
    }
    else if(s_menu_view.draw_shortcut_at)
    {
        const char *name = menu_shortcut_name(index);
        if(name)
            s_menu_view.draw_shortcut_at(slot, name, selected);
    }
}

static void menu_draw_selection(uint8_t previous)
{
    if(previous / MENU_PAGE_ITEMS != s_menu_index / MENU_PAGE_ITEMS)
    {
        menu_render();
        return;
    }
    menu_draw_title_if_ready(menu_item_title(s_menu_index));
    menu_draw_item(previous);
    menu_draw_item(s_menu_index);
}

uint8_t hk_menu_index_get(void)
{
    return s_menu_index;
}

const hk_app_t *hk_menu_active_app(void)
{
    return s_active_app;
}

void menu_render(void)
{
    uint8_t count = menu_item_count();
    s_rendered_count = count;
    hk_back_exit_set_armed(0);
    if(s_menu_index >= count)
        s_menu_index = 0U;
    if(count == 0U)
    {
        menu_draw_chrome_if_ready("NO APPS");
        return;
    }
    menu_draw_chrome_if_ready(menu_item_title(s_menu_index));
    for(uint8_t index = 0; index < count; index++)
        menu_draw_item(index);
    if(s_menu_view.draw_page)
        s_menu_view.draw_page(s_menu_index / MENU_PAGE_ITEMS,
            (count + MENU_PAGE_ITEMS - 1U) / MENU_PAGE_ITEMS);
}

void shell_show_menu_reason(hk_app_stop_reason_t reason)
{
    const hk_app_t *app = s_active_app;
    s_active_app = NULL;

    if(s_owner_hooks.exit)
        s_owner_hooks.exit(app, reason);
    hk_screen_set(SCREEN_MENU);
    hk_back_exit_set_armed(0);
    menu_render();
    activity_note();
    printf("[SHELL] screen MENU item=%s\r\n",
           menu_item_title(s_menu_index));
}

void shell_show_menu(void)
{
    shell_show_menu_reason(HK_APP_STOP_BACK);
}

uint8_t shell_open_app(const hk_app_t *app, const hk_input_snapshot_t *input)
{
    uint8_t index;

    if(!app || !s_owner_hooks.enter)
        return 0U;
    for(index = 0U; index < g_menu_item_count; index++)
    {
        if(g_menu_items[index] == app)
            break;
    }
    if(index < g_menu_item_count)
        s_menu_index = index;
    s_menu_repeat_button = 0;
    s_menu_repeat_ticks = 0;
    printf("[MENU] open %s\r\n", app->title);
    if(s_owner_hooks.enter)
    {
        if(!s_owner_hooks.enter(app, input))
            return 0U;
    }
    s_active_app = app;
    hk_screen_set(SCREEN_APP);
    return 1U;
}

void menu_owner_hooks_set(const hk_menu_owner_hooks_t *hooks)
{
    if(hooks)
        s_owner_hooks = *hooks;
    else
        memset(&s_owner_hooks, 0, sizeof(s_owner_hooks));
}

uint8_t shell_open_shortcut(const char *name, const hk_input_snapshot_t *input)
{
    if(!name || !s_owner_hooks.open_shortcut)
        return 0U;
    for(uint8_t index = MENU_ITEM_COUNT, count = menu_item_count(); index < count; index++)
    {
        const char *shortcut = menu_shortcut_name(index);
        if(shortcut && strcmp(shortcut, name) == 0)
        {
            printf("[MENU] run %s\r\n", shortcut);
            uint8_t opened = s_owner_hooks.open_shortcut(shortcut, input);
            s_menu_index = index;
            return opened;
        }
    }
    return 0U;
}

void shell_open_selected(const hk_input_snapshot_t *input)
{
    if(menu_item_count() != s_rendered_count)
    {
        menu_render();
        return;
    }
    if(s_menu_index < MENU_ITEM_COUNT)
    {
        (void)shell_open_app(g_menu_items[s_menu_index], input);
        return;
    }
    if(!shell_open_shortcut(menu_shortcut_name(s_menu_index), input))
        menu_render();
}

void menu_select_delta(int8_t delta)
{
    uint8_t count = menu_item_count();
    if(count != s_rendered_count)
        menu_render();
    if(count == 0U)
        return;

    uint8_t previous = s_menu_index;
    if(delta < 0)
        s_menu_index = s_menu_index == 0 ? count - 1 : s_menu_index - 1;
    else if(delta > 0)
        s_menu_index = (uint8_t)((s_menu_index + 1) % count);

    if(previous != s_menu_index)
    {
        menu_draw_selection(previous);
    }
    printf("[MENU] select %s\r\n", menu_item_title(s_menu_index));
}

void menu_select_vertical(void)
{
    uint8_t count = menu_item_count();
    if(count != s_rendered_count)
        menu_render();
    if(count == 0U)
        return;
    uint8_t previous = s_menu_index;
    s_menu_index = (uint8_t)((s_menu_index + MENU_GRID_COLS) % count);
    if(previous != s_menu_index)
    {
        menu_draw_selection(previous);
    }
    printf("[MENU] select %s\r\n", menu_item_title(s_menu_index));
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
    if(menu_item_count() != s_rendered_count)
        menu_render();

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

uint8_t shell_open_app_id(const char *id, const hk_input_snapshot_t *input)
{
    const hk_app_t *app = hk_app_for_id(id);
    if(!app)
        return 0U;
    if(s_active_app)
        shell_show_menu_reason(HK_APP_STOP_SWITCH);
    return shell_open_app(app, input);
}
