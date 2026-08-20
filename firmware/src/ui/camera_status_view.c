#include "camera_status_view.h"

#include "../config/display_config.h"
#include "../config/menu_layout.h"

#include "../ui/display_binding.h"

void camera_status_view_draw(const char *line1, const char *line2)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
    hk_ui_display_draw_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, MENU_LINE, COLOR_TERM_GREEN);
    hk_ui_display_draw_text_centered(78, line1, COLOR_TERM_GREEN, COLOR_BLACK);
    hk_ui_display_draw_text_centered(108, line2, COLOR_TERM_GREEN, COLOR_BLACK);
}
