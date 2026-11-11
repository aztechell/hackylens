#include "face_recognition_app.h"
#include "../../ui/menu_icons.h"

void face_recognition_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_face_recognition);
}
