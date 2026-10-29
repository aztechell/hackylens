#include "apriltag_view.h"

#include <stdio.h>
#include <string.h>

#include "../../config/camera_config.h"
#include "../../config/camera_layout.h"
#include "../../config/display_config.h"
#include "../../ui/display_binding.h"
#include "../../ui/menu_icons.h"
#include "../../ui/camera_view.h"

#define APRILTAG_SELECTED_COLOR 0xFFE0U

void apriltag_view_compose_results(camera_view_present_t *present,
                                   uint16_t width,
                                   uint16_t height,
                                   const apriltag_result_t *results,
                                   const uint8_t *selected,
                                   uint8_t count)
{
    camera_view_frame_t view = {0};
    camera_view_rect_t rects[APRILTAG_RESULT_MAX];

    if(!present || !results)
        return;
    if(count > APRILTAG_RESULT_MAX)
        count = APRILTAG_RESULT_MAX;
    for(uint8_t i = 0; i < count; i++)
    {
        rects[i].x = results[i].x;
        rects[i].y = results[i].y;
        rects[i].w = results[i].w;
        rects[i].h = results[i].h;
    }
    view.width = width;
    view.height = height;
    view.rotate = CAMERA_PREVIEW_ROTATE;
    view.fit = CAMERA_PREVIEW_FIT;
    for(uint8_t i = 0U; i < count; i++)
        camera_view_compose_rects(present, &view, &rects[i], 1U,
                                  selected && selected[i] ? APRILTAG_SELECTED_COLOR : COLOR_TERM_GREEN);

    for(uint8_t i = 0; i < count; i++)
    {
        char label[16];
        uint16_t label_x;
        uint16_t label_y;
        uint16_t label_width;
        const camera_view_rect_t center = {
            results[i].center_x, results[i].center_y, 1, 1
        };
        camera_view_rect_t mapped_center;

        if(!camera_view_map_rect(&view, &center, &mapped_center))
            continue;

        snprintf(label, sizeof(label), "%u", (unsigned)results[i].id);
        label_width = (uint16_t)(strlen(label) * HACKYLENS_FONT_W);
        label_x = mapped_center.x > (int16_t)(label_width / 2U) ?
            (uint16_t)(mapped_center.x - label_width / 2U) : 0U;
        label_y = mapped_center.y > (int16_t)(HACKYLENS_FONT_H / 2U) ?
            (uint16_t)(mapped_center.y - HACKYLENS_FONT_H / 2U) : 0U;
        if(label_x + label_width > present->width)
            label_x = present->width > label_width ? present->width - label_width : 0;
        if(label_y + HACKYLENS_FONT_H > present->height)
            label_y = present->height > HACKYLENS_FONT_H ? present->height - HACKYLENS_FONT_H : 0;
        camera_view_compose_text_at(present, label_x, label_y, label,
                                    selected && selected[i] ? APRILTAG_SELECTED_COLOR : COLOR_TERM_GREEN,
                                    COLOR_BLACK);
    }
    camera_view_compose_crosshair(present, present->width / 2U, present->height / 2U, COLOR_WHITE);
}

void apriltag_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_apriltag);
}
