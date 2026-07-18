#include "face_detect_view.h"

#include "../../config/display_config.h"
#include "../../drivers/hk_lcd.h"
#include "../../ui/camera_view.h"

void face_detect_view_draw_boxes(uint16_t width, uint16_t height,
                                 const face_detect_box_t *boxes, uint8_t count)
{
    camera_view_frame_t view = {0};
    camera_view_rect_t rects[FACE_DETECT_BOX_MAX];

    if(count > FACE_DETECT_BOX_MAX)
        count = FACE_DETECT_BOX_MAX;
    for(uint8_t i = 0; i < count; i++)
    {
        rects[i].x = boxes[i].x;
        rects[i].y = boxes[i].y;
        rects[i].w = boxes[i].w;
        rects[i].h = boxes[i].h;
    }
    view.width = width;
    view.height = height;
    camera_view_draw_rects(&view, rects, count, COLOR_TERM_GREEN);
}

void face_detect_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    lcd_draw_rect(x + 15, y + 10, 30, 34, 2, color);
    lcd_fill_rect(x + 23, y + 22, 4, 4, color);
    lcd_fill_rect(x + 34, y + 22, 4, 4, color);
    lcd_fill_rect(x + 25, y + 34, 12, 2, color);
}
