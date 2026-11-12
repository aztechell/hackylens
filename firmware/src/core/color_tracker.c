#include "color_tracker.h"

#include <stddef.h>

#define NO_LABEL UINT16_MAX

static uint8_t threshold_valid(const hk_color_threshold_t *threshold)
{
    return threshold && threshold->red_min <= threshold->red_max &&
           threshold->green_min <= threshold->green_max &&
           threshold->blue_min <= threshold->blue_max &&
           threshold->red_max <= 31U && threshold->green_max <= 63U &&
           threshold->blue_max <= 31U;
}

static uint8_t matches(uint16_t pixel, const hk_color_threshold_t *threshold)
{
    uint8_t red = pixel >> 11U;
    uint8_t green = (pixel >> 5U) & 63U;
    uint8_t blue = pixel & 31U;
    return red >= threshold->red_min && red <= threshold->red_max &&
           green >= threshold->green_min && green <= threshold->green_max &&
           blue >= threshold->blue_min && blue <= threshold->blue_max;
}

static uint16_t root_of(hk_color_tracker_t *tracker, uint16_t label)
{
    while(tracker->components[label].parent != label)
    {
        tracker->components[label].parent =
            tracker->components[tracker->components[label].parent].parent;
        label = tracker->components[label].parent;
    }
    return label;
}

static void include_blob(hk_color_blob_t *blob, const hk_color_blob_t *other)
{
    uint16_t right = blob->bounds.x + blob->bounds.width;
    uint16_t bottom = blob->bounds.y + blob->bounds.height;
    uint16_t other_right = other->bounds.x + other->bounds.width;
    uint16_t other_bottom = other->bounds.y + other->bounds.height;
    if(other->bounds.x < blob->bounds.x) blob->bounds.x = other->bounds.x;
    if(other->bounds.y < blob->bounds.y) blob->bounds.y = other->bounds.y;
    if(other_right > right) right = other_right;
    if(other_bottom > bottom) bottom = other_bottom;
    blob->bounds.width = right - blob->bounds.x;
    blob->bounds.height = bottom - blob->bounds.y;
    blob->pixels += other->pixels;
}

static uint16_t merge(hk_color_tracker_t *tracker, uint16_t first, uint16_t second)
{
    if(first == second)
        return first;
    if(second < first)
    {
        uint16_t swap = first;
        first = second;
        second = swap;
    }
    include_blob(&tracker->components[first].blob, &tracker->components[second].blob);
    tracker->components[second].parent = first;
    return first;
}

static void consider(hk_color_blob_t *best, const hk_color_blob_t *candidate,
                     uint32_t min_pixels)
{
    if(candidate->pixels < min_pixels)
        return;
    if(candidate->pixels > best->pixels ||
       (candidate->pixels == best->pixels &&
        (candidate->bounds.y < best->bounds.y ||
         (candidate->bounds.y == best->bounds.y && candidate->bounds.x < best->bounds.x))))
        *best = *candidate;
}

static uint16_t retire_row(hk_color_tracker_t *tracker, hk_color_run_t *runs,
                            uint16_t run_count, uint16_t component_count,
                            uint32_t min_pixels, hk_color_blob_t *best)
{
    for(uint16_t i = 0U; i < component_count; i++)
        tracker->remap[i] = NO_LABEL;
    for(uint16_t i = 0U; i < run_count; i++)
    {
        runs[i].label = root_of(tracker, runs[i].label);
        tracker->remap[runs[i].label] = 0U;
    }

    uint16_t live = 0U;
    for(uint16_t i = 0U; i < component_count; i++)
    {
        if(tracker->components[i].parent != i)
            continue;
        if(tracker->remap[i] == NO_LABEL)
        {
            consider(best, &tracker->components[i].blob, min_pixels);
            continue;
        }
        tracker->components[live] = tracker->components[i];
        tracker->components[live].parent = live;
        tracker->remap[i] = live++;
    }
    for(uint16_t i = 0U; i < run_count; i++)
        runs[i].label = tracker->remap[runs[i].label];
    return live;
}

static uint16_t read_pixel(const hk_color_frame_t *frame, uint16_t x, uint16_t y)
{
    uint32_t offset = frame->rotate ?
        (uint32_t)(frame->height - 1U - x) * frame->width + y :
        (uint32_t)y * frame->width + x;
    return frame->pixels[offset];
}

uint8_t hk_color_find_largest(hk_color_tracker_t *tracker,
                              const hk_color_frame_t *frame,
                              const hk_color_rect_t *roi,
                              const hk_color_threshold_t *threshold,
                              uint32_t min_pixels, hk_color_blob_t *blob)
{
    if(!tracker || !frame || !frame->pixels || !frame->width || !frame->height ||
       frame->width > HK_COLOR_MAX_WIDTH || frame->height > HK_COLOR_MAX_HEIGHT ||
       !threshold_valid(threshold) || !min_pixels || !blob)
        return 0U;
    uint16_t width = frame->rotate ? frame->height : frame->width;
    uint16_t height = frame->rotate ? frame->width : frame->height;
    hk_color_rect_t full = {0U, 0U, width, height};
    if(!roi)
        roi = &full;
    if(!roi->width || !roi->height || (uint32_t)roi->x + roi->width > width ||
       (uint32_t)roi->y + roi->height > height)
        return 0U;

    uint16_t end_x = roi->x + roi->width;
    uint16_t end_y = roi->y + roi->height;
    uint16_t previous_count = 0U, component_count = 0U;
    *blob = (hk_color_blob_t){0};
    for(uint16_t y = roi->y; y < end_y; y++)
    {
        hk_color_run_t *current = tracker->rows[y & 1U];
        const hk_color_run_t *previous = tracker->rows[(y & 1U) ^ 1U];
        uint16_t current_count = 0U, previous_cursor = 0U;
        uint16_t x = roi->x;
        while(x < end_x)
        {
            if(!matches(read_pixel(frame, x, y), threshold))
            {
                x++;
                continue;
            }
            uint16_t start = x++;
            while(x < end_x && matches(read_pixel(frame, x, y), threshold))
                x++;

            while(previous_cursor < previous_count && previous[previous_cursor].x1 <= start)
                previous_cursor++;
            uint16_t label = NO_LABEL;
            for(uint16_t i = previous_cursor; i < previous_count && previous[i].x0 < x; i++)
            {
                uint16_t neighbor = root_of(tracker, previous[i].label);
                label = label == NO_LABEL ? neighbor : merge(tracker, label, neighbor);
            }
            const hk_color_blob_t run = {{start, y, x - start, 1U}, x - start};
            if(label == NO_LABEL)
            {
                label = component_count++;
                tracker->components[label] = (hk_color_component_t){run, label};
            }
            else
                include_blob(&tracker->components[label].blob, &run);
            current[current_count++] = (hk_color_run_t){start, x, label};
        }
        component_count = retire_row(tracker, current, current_count,
                                     component_count, min_pixels, blob);
        previous_count = current_count;
    }
    for(uint16_t i = 0U; i < component_count; i++)
        consider(blob, &tracker->components[i].blob, min_pixels);
    return 1U;
}
