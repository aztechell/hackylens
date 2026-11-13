#include "camera_status_view.h"

#include "../config/display_config.h"
#include "../config/menu_layout.h"

#include "../ui/display_binding.h"

void camera_status_view_draw(const char *line1, const char *line2)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
    hk_ui_display_draw_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, MENU_LINE, hk_ui_accent());
    hk_ui_display_draw_text_centered(78, line1, hk_ui_accent(), COLOR_BLACK);
    hk_ui_display_draw_text_centered(108, line2, hk_ui_accent(), COLOR_BLACK);
}
