#ifndef HK_MENU_LAYOUT_H
#define HK_MENU_LAYOUT_H

#include "display_config.h"

#define MENU_BAR_H 30
#define MENU_LINE 2
#if HK_DISPLAY_REQUIRED_WIDTH < 320
#define MENU_GRID_COLS 3
#else
#define MENU_GRID_COLS 4
#endif
#define MENU_GRID_ROWS 3
#define MENU_PAGE_ITEMS (MENU_GRID_COLS * MENU_GRID_ROWS)
#define MENU_ICON 60
#define MENU_ICON_GAP_X 18
#define MENU_ICON_GAP_Y 4
#define MENU_ICON_X0 12
#define MENU_ICON_Y0 36
#define MENU_REPEAT_INITIAL_TICKS 25
#define MENU_REPEAT_NEXT_TICKS 9

#endif
