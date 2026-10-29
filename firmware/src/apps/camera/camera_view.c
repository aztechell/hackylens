#include "camera_view.h"

#include "../../ui/display_binding.h"
#include "../../ui/menu_icons.h"

void camera_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_camera);
}
