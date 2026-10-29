#ifndef HK_DISPLAY_CONFIG_H
#define HK_DISPLAY_CONFIG_H

#include "hackylens_font_1bpp.h"
#include "hackylens_font_cyrillic_1bpp.h"

#if defined(__has_include)
#if __has_include("board_config.h")
#include "board_config.h"
#endif
#endif
#ifndef HK_BOARD_UI_WIDTH
#define HK_BOARD_UI_WIDTH 320
#define HK_BOARD_UI_HEIGHT 240
#endif
#define HK_DISPLAY_REQUIRED_WIDTH HK_BOARD_UI_WIDTH
#define HK_DISPLAY_REQUIRED_HEIGHT HK_BOARD_UI_HEIGHT

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_TERM_GREEN 0x07E0

#define TERM_COLS (HK_DISPLAY_REQUIRED_WIDTH / HACKYLENS_FONT_W)

#endif
