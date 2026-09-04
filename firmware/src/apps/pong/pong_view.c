#include "pong_view.h"

#include <stdio.h>
#include <string.h>

#define PONG_DIRTY_REGION_MAX HK_APP_MAX_INVALIDATIONS

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} pong_rect_t;

typedef struct
{
    pong_rect_t regions[PONG_DIRTY_REGION_MAX];
    uint8_t count;
} pong_dirty_list_t;

static uint8_t pong_rect_intersection(pong_rect_t first, pong_rect_t second,
                                      pong_rect_t *result)
{
    int16_t left = first.x > second.x ? first.x : second.x;
    int16_t top = first.y > second.y ? first.y : second.y;
    int16_t right_first = first.x + first.w;
    int16_t right_second = second.x + second.w;
    int16_t bottom_first = first.y + first.h;
    int16_t bottom_second = second.y + second.h;
    int16_t right = right_first < right_second ? right_first : right_second;
    int16_t bottom = bottom_first < bottom_second ? bottom_first : bottom_second;

    if(left >= right || top >= bottom)
        return 0U;
    result->x = left;
    result->y = top;
    result->w = right - left;
    result->h = bottom - top;
    return 1U;
}

static uint8_t pong_rect_clip_screen(pong_rect_t *rect)
{
    const pong_rect_t screen = {
        0, 0, PONG_DISPLAY_WIDTH, PONG_DISPLAY_HEIGHT,
    };
    pong_rect_t clipped;

    if(!pong_rect_intersection(*rect, screen, &clipped))
        return 0U;
    *rect = clipped;
    return 1U;
}

static uint8_t pong_rects_touch(pong_rect_t first, pong_rect_t second)
{
    return first.x <= second.x + second.w && second.x <= first.x + first.w &&
           first.y <= second.y + second.h && second.y <= first.y + first.h;
}

static pong_rect_t pong_rect_union(pong_rect_t first, pong_rect_t second)
{
    pong_rect_t result;
    int16_t right_first = first.x + first.w;
    int16_t right_second = second.x + second.w;
    int16_t bottom_first = first.y + first.h;
    int16_t bottom_second = second.y + second.h;
    int16_t right;
    int16_t bottom;

    result.x = first.x < second.x ? first.x : second.x;
    result.y = first.y < second.y ? first.y : second.y;
    right = right_first > right_second ? right_first : right_second;
    bottom = bottom_first > bottom_second ? bottom_first : bottom_second;
    result.w = right - result.x;
    result.h = bottom - result.y;
    return result;
}

static uint32_t pong_rect_area(pong_rect_t rect)
{
    return (uint32_t)rect.w * (uint32_t)rect.h;
}

static void pong_dirty_add(pong_dirty_list_t *dirty, pong_rect_t region)
{
    uint8_t index = 0U;

    if(!pong_rect_clip_screen(&region))
        return;
    while(index < dirty->count)
    {
        if(!pong_rects_touch(dirty->regions[index], region))
        {
            index++;
            continue;
        }
        region = pong_rect_union(dirty->regions[index], region);
        dirty->count--;
        dirty->regions[index] = dirty->regions[dirty->count];
        index = 0U;
    }
    if(dirty->count < PONG_DIRTY_REGION_MAX)
    {
        dirty->regions[dirty->count] = region;
        dirty->count++;
        return;
    }

    {
        uint8_t best = 0U;
        pong_rect_t best_union = pong_rect_union(dirty->regions[0], region);
        uint32_t best_growth = pong_rect_area(best_union) -
            pong_rect_area(dirty->regions[0]);

        for(uint8_t candidate = 1U; candidate < dirty->count; candidate++)
        {
            pong_rect_t combined = pong_rect_union(
                dirty->regions[candidate], region);
            uint32_t growth = pong_rect_area(combined) -
                pong_rect_area(dirty->regions[candidate]);

            if(growth < best_growth)
            {
                best = candidate;
                best_union = combined;
                best_growth = growth;
            }
        }
        dirty->regions[best] = best_union;
    }
}

static hk_display_rect_t pong_to_display_rect(pong_rect_t rect)
{
    hk_display_rect_t result = {
        rect.x, rect.y, (uint32_t)rect.w, (uint32_t)rect.h,
    };

    return result;
}

static hk_result_t pong_fill_shape(
    hk_app_surface_t *surface,
    pong_rect_t shape,
    const pong_rect_t *clip,
    uint16_t color)
{
    pong_rect_t visible = shape;
    hk_display_rect_t rect;

    if(clip && !pong_rect_intersection(visible, *clip, &visible))
        return HK_OK;
    if(!pong_rect_clip_screen(&visible))
        return HK_OK;
    rect = pong_to_display_rect(visible);
    return hk_app_surface_fill_rect(surface, &rect, color);
}

static pong_rect_t pong_title_rect(void)
{
    pong_rect_t result = {
        PONG_MENU_LINE,
        PONG_MENU_LINE,
        PONG_DISPLAY_WIDTH - PONG_MENU_LINE * 2,
        PONG_MENU_BAR_H - PONG_MENU_LINE * 2,
    };

    return result;
}

static hk_result_t pong_view_draw_title(
    hk_app_surface_t *surface, pong_view_state_t state)
{
    char title[24];
    pong_rect_t bar = pong_title_rect();
    hk_display_rect_t bounds;
    hk_result_t result;

    snprintf(
        title, sizeof(title), "YOU  %u : %u  CPU",
        (unsigned)state.player_score, (unsigned)state.ai_score);
    result = pong_fill_shape(surface, bar, NULL, PONG_COLOR_BLACK);
    if(result != HK_OK)
        return result;
    bounds = pong_to_display_rect(bar);
    return hk_app_surface_text(
        surface, &bounds, title, (uint32_t)strlen(title), PONG_COLOR_GREEN);
}

static hk_result_t pong_view_draw_chrome(
    hk_app_surface_t *surface, const pong_rect_t *clip)
{
    const pong_rect_t edges[] = {
        {0, 0, PONG_DISPLAY_WIDTH, PONG_MENU_LINE},
        {0, PONG_DISPLAY_HEIGHT - PONG_MENU_LINE, PONG_DISPLAY_WIDTH, PONG_MENU_LINE},
        {0, 0, PONG_MENU_LINE, PONG_DISPLAY_HEIGHT},
        {PONG_DISPLAY_WIDTH - PONG_MENU_LINE, 0, PONG_MENU_LINE, PONG_DISPLAY_HEIGHT},
        {0, PONG_MENU_BAR_H - PONG_MENU_LINE, PONG_DISPLAY_WIDTH, PONG_MENU_LINE},
    };
    uint8_t index;
    hk_result_t result;

    for(index = 0U; index < (uint8_t)(sizeof(edges) / sizeof(edges[0])); index++)
    {
        result = pong_fill_shape(surface, edges[index], clip, PONG_COLOR_GREEN);
        if(result != HK_OK)
            return result;
    }
    return HK_OK;
}

static hk_result_t pong_view_draw_border(
    hk_app_surface_t *surface, const pong_rect_t *clip)
{
    const pong_rect_t edges[] = {
        {PONG_FIELD_X, PONG_FIELD_Y, PONG_FIELD_W, PONG_MENU_LINE},
        {PONG_FIELD_X, PONG_FIELD_Y + PONG_FIELD_H - PONG_MENU_LINE,
         PONG_FIELD_W, PONG_MENU_LINE},
        {PONG_FIELD_X, PONG_FIELD_Y, PONG_MENU_LINE, PONG_FIELD_H},
        {PONG_FIELD_X + PONG_FIELD_W - PONG_MENU_LINE, PONG_FIELD_Y,
         PONG_MENU_LINE, PONG_FIELD_H},
    };
    uint8_t index;
    hk_result_t result;

    for(index = 0U; index < (uint8_t)(sizeof(edges) / sizeof(edges[0])); index++)
    {
        result = pong_fill_shape(surface, edges[index], clip, PONG_COLOR_GREEN);
        if(result != HK_OK)
            return result;
    }
    return HK_OK;
}

static hk_result_t pong_view_draw_midline(
    hk_app_surface_t *surface, const pong_rect_t *clip)
{
    const pong_rect_t line = {
        (int16_t)(PONG_FIELD_X + PONG_MENU_LINE),
        (int16_t)(PONG_FIELD_Y + PONG_FIELD_H / 2 - 1),
        (int16_t)(PONG_FIELD_W - PONG_MENU_LINE * 2),
        2,
    };

    /* One fill keeps the first frame inside the K210 32-command BASE batch.
     * A dashed line used 19 segments and overflowed on launch. */
    return pong_fill_shape(surface, line, clip, PONG_COLOR_GREEN);
}

static pong_rect_t pong_view_trail_rect(pong_view_state_t state, uint8_t index)
{
    int16_t size = index == 0U ? PONG_BALL_SIZE - 2 : PONG_BALL_SIZE - 4;
    int16_t inset = (PONG_BALL_SIZE - size) / 2;
    pong_rect_t result = {
        state.trail_x[index] + inset,
        state.trail_y[index] + inset,
        size,
        size,
    };

    return result;
}

static hk_result_t pong_view_draw_trail(
    hk_app_surface_t *surface,
    pong_view_state_t state,
    uint16_t color,
    const pong_rect_t *clip)
{
    uint8_t index;
    hk_result_t result = HK_OK;

    for(index = 0U;
        result == HK_OK && index < state.trail_count && index < PONG_TRAIL_LENGTH;
        index++)
        result = pong_fill_shape(
            surface, pong_view_trail_rect(state, index), clip, color);
    return result;
}

static hk_result_t pong_view_draw_objects(
    hk_app_surface_t *surface,
    pong_view_state_t state,
    uint16_t paddle_color,
    uint16_t ball_color,
    const pong_rect_t *clip)
{
    const pong_rect_t player = {
        state.player_x, PONG_PLAYER_Y, PONG_PADDLE_W, PONG_PADDLE_H,
    };
    const pong_rect_t ai = {
        state.ai_x, PONG_AI_Y, PONG_PADDLE_W, PONG_PADDLE_H,
    };
    const pong_rect_t ball = {
        state.ball_x, state.ball_y, PONG_BALL_SIZE, PONG_BALL_SIZE,
    };
    hk_result_t result = pong_fill_shape(surface, player, clip, paddle_color);

    if(result != HK_OK)
        return result;
    result = pong_fill_shape(surface, ai, clip, paddle_color);
    if(result != HK_OK)
        return result;
    return pong_fill_shape(surface, ball, clip, ball_color);
}

static pong_rect_t pong_view_flash_rect(pong_view_state_t state)
{
    const pong_rect_t result = {state.flash_x - 5, state.flash_y - 5, 11, 11};

    return result;
}

static hk_result_t pong_view_draw_flash(
    hk_app_surface_t *surface,
    pong_view_state_t state,
    uint16_t color,
    const pong_rect_t *clip)
{
    const pong_rect_t horizontal = {state.flash_x - 5, state.flash_y - 1, 11, 3};
    const pong_rect_t vertical = {state.flash_x - 1, state.flash_y - 5, 3, 11};
    hk_result_t result;

    if(state.flash_ticks == 0U)
        return HK_OK;
    result = pong_fill_shape(surface, horizontal, clip, color);
    if(result != HK_OK)
        return result;
    return pong_fill_shape(surface, vertical, clip, color);
}

static hk_result_t pong_view_draw_dynamic(
    hk_app_surface_t *surface,
    pong_view_state_t state,
    const pong_rect_t *clip)
{
    hk_result_t result = pong_view_draw_trail(
        surface, state, PONG_TRAIL_COLOR, clip);

    if(result != HK_OK)
        return result;
    result = pong_view_draw_objects(
        surface, state, PONG_COLOR_GREEN, PONG_BALL_COLOR, clip);
    if(result != HK_OK)
        return result;
    return pong_view_draw_flash(surface, state, PONG_FLASH_COLOR, clip);
}

static uint8_t pong_view_trail_changed(
    pong_view_state_t previous, pong_view_state_t current)
{
    uint8_t index;

    if(previous.trail_count != current.trail_count)
        return 1U;
    for(index = 0U; index < previous.trail_count && index < PONG_TRAIL_LENGTH; index++)
    {
        if(previous.trail_x[index] != current.trail_x[index] ||
           previous.trail_y[index] != current.trail_y[index])
            return 1U;
    }
    return 0U;
}

static uint8_t pong_view_flash_changed(
    pong_view_state_t previous, pong_view_state_t current)
{
    uint8_t previous_visible = previous.flash_ticks > 0U;
    uint8_t current_visible = current.flash_ticks > 0U;

    return previous_visible != current_visible ||
           (previous_visible && (previous.flash_x != current.flash_x ||
                                 previous.flash_y != current.flash_y));
}

static void pong_dirty_add_trail(
    pong_dirty_list_t *dirty, pong_view_state_t state)
{
    uint8_t index;

    for(index = 0U; index < state.trail_count && index < PONG_TRAIL_LENGTH; index++)
        pong_dirty_add(dirty, pong_view_trail_rect(state, index));
}

static pong_dirty_list_t pong_view_dirty_regions(
    pong_view_state_t previous, pong_view_state_t current)
{
    pong_dirty_list_t dirty = {{{0, 0, 0, 0}}, 0U};

    if(previous.player_x != current.player_x)
    {
        pong_dirty_add(&dirty, (pong_rect_t){
            previous.player_x, PONG_PLAYER_Y, PONG_PADDLE_W, PONG_PADDLE_H,
        });
        pong_dirty_add(&dirty, (pong_rect_t){
            current.player_x, PONG_PLAYER_Y, PONG_PADDLE_W, PONG_PADDLE_H,
        });
    }
    if(previous.ai_x != current.ai_x)
    {
        pong_dirty_add(&dirty, (pong_rect_t){
            previous.ai_x, PONG_AI_Y, PONG_PADDLE_W, PONG_PADDLE_H,
        });
        pong_dirty_add(&dirty, (pong_rect_t){
            current.ai_x, PONG_AI_Y, PONG_PADDLE_W, PONG_PADDLE_H,
        });
    }
    if(previous.ball_x != current.ball_x || previous.ball_y != current.ball_y)
    {
        pong_dirty_add(&dirty, (pong_rect_t){
            previous.ball_x, previous.ball_y, PONG_BALL_SIZE, PONG_BALL_SIZE,
        });
        pong_dirty_add(&dirty, (pong_rect_t){
            current.ball_x, current.ball_y, PONG_BALL_SIZE, PONG_BALL_SIZE,
        });
    }
    if(pong_view_trail_changed(previous, current))
    {
        pong_dirty_add_trail(&dirty, previous);
        pong_dirty_add_trail(&dirty, current);
    }
    if(pong_view_flash_changed(previous, current))
    {
        if(previous.flash_ticks > 0U)
            pong_dirty_add(&dirty, pong_view_flash_rect(previous));
        if(current.flash_ticks > 0U)
            pong_dirty_add(&dirty, pong_view_flash_rect(current));
    }
    return dirty;
}

static hk_result_t pong_view_restore_region(
    hk_app_surface_t *surface,
    pong_view_state_t current,
    pong_rect_t region)
{
    hk_result_t result = pong_fill_shape(surface, region, NULL, PONG_COLOR_BLACK);

    if(result != HK_OK)
        return result;
    result = pong_view_draw_chrome(surface, &region);
    if(result != HK_OK)
        return result;
    result = pong_view_draw_border(surface, &region);
    if(result != HK_OK)
        return result;
    result = pong_view_draw_midline(surface, &region);
    if(result != HK_OK)
        return result;
    return pong_view_draw_dynamic(surface, current, &region);
}

hk_result_t pong_view_render_initial(
    hk_app_surface_t *surface, pong_view_state_t state)
{
    hk_result_t result;

    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_surface_clear(surface, PONG_COLOR_BLACK);
    if(result != HK_OK)
        return result;
    result = pong_view_draw_chrome(surface, NULL);
    if(result != HK_OK)
        return result;
    result = pong_view_draw_border(surface, NULL);
    if(result != HK_OK)
        return result;
    result = pong_view_draw_midline(surface, NULL);
    if(result != HK_OK)
        return result;
    result = pong_view_draw_title(surface, state);
    if(result != HK_OK)
        return result;
    return pong_view_draw_dynamic(surface, state, NULL);
}

hk_result_t pong_view_render_score(
    hk_app_surface_t *surface, pong_view_state_t state)
{
    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    return pong_view_draw_title(surface, state);
}

hk_result_t pong_view_render_frame(
    hk_app_surface_t *surface,
    pong_view_state_t previous,
    pong_view_state_t current)
{
    pong_dirty_list_t dirty;
    uint8_t index;
    hk_result_t result = HK_OK;

    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    dirty = pong_view_dirty_regions(previous, current);
    for(index = 0U; result == HK_OK && index < dirty.count; index++)
        result = pong_view_restore_region(surface, current, dirty.regions[index]);
    return result;
}

uint8_t pong_view_collect_invalidations(
    pong_view_state_t previous,
    pong_view_state_t current,
    uint8_t score_changed,
    hk_display_rect_t *regions,
    uint8_t max_regions,
    uint8_t *full)
{
    pong_dirty_list_t dirty;
    uint8_t index;

    if(full)
        *full = 0U;
    if(!regions || !full || max_regions == 0U)
        return 0U;
    dirty = pong_view_dirty_regions(previous, current);
    if(score_changed)
    {
        if(dirty.count >= PONG_DIRTY_REGION_MAX)
        {
            *full = 1U;
            return 0U;
        }
        pong_dirty_add(&dirty, pong_title_rect());
    }
    if(dirty.count > max_regions)
    {
        *full = 1U;
        return 0U;
    }
    for(index = 0U; index < dirty.count; index++)
        regions[index] = pong_to_display_rect(dirty.regions[index]);
    return dirty.count;
}
