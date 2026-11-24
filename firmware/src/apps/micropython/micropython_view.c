#include "micropython_view.h"

#include <stdio.h>
#include <string.h>

#include "../../config/display_config.h"
#include "../../core/hk_string.h"
#include "../../ui/display_binding.h"
#include "../../ui/menu_icons.h"
#include "../../ui/hk_ui.h"

#define PY_INFO_Y 30U
#define PY_INFO_H 22U
#define PY_BODY_Y 53U
#define PY_BODY_H 187U
#define PY_TEXT_X 8U
#define PY_TEXT_COLUMNS 25U
#define PY_LIST_ROW_H 24U
#define PY_LIST_ROW_PITCH 26U
#define PY_ACTION_ROW_Y0 57U
#define PY_ACTION_ROW_H 26U
#define PY_ACTION_ROW_PITCH 29U
#define PY_TEXT_ROW_PITCH 20U
#define PY_FONT_W 12U
#define PY_FONT_H 20U
#define PY_MAX_GLYPHS (HK_DISPLAY_REQUIRED_WIDTH / PY_FONT_W)
#define PY_CODE_FONT_W 8U
#define PY_CODE_FONT_H 14U
#define PY_CODE_ROW_PITCH 15U
#define PY_CODE_MARGIN 6U
#define PY_CODE_NUMBER_COLS 6U
#define PY_CODE_X (PY_CODE_MARGIN + PY_CODE_NUMBER_COLS * PY_CODE_FONT_W + 4U)
#define PY_CODE_COLUMNS ((HK_DISPLAY_REQUIRED_WIDTH - PY_CODE_MARGIN - PY_CODE_X) / PY_CODE_FONT_W)
#define PY_CODE_MAX_GLYPHS (HK_DISPLAY_REQUIRED_WIDTH / PY_CODE_FONT_W)

_Static_assert(PY_ACTION_ROW_Y0 + (MICROPYTHON_ACTION_COUNT - 1U) *
               PY_ACTION_ROW_PITCH + PY_ACTION_ROW_H <= HK_DISPLAY_REQUIRED_HEIGHT,
               "all script actions must fit on screen");
_Static_assert(MICROPYTHON_PREVIEW_LINES * PY_CODE_ROW_PITCH <= PY_BODY_H,
               "all source rows must fit on screen");
_Static_assert(HACKYLENS_FONT_W <= 2U * PY_CODE_FONT_W &&
               HACKYLENS_FONT_H <= 2U * PY_CODE_FONT_H,
               "the text palette covers at most four source pixels per sample");

static uint16_t blend_color(uint16_t foreground, uint16_t background,
                            uint8_t coverage)
{
    uint16_t inverse = (uint16_t)(255U - coverage);
    uint16_t red = (uint16_t)((((foreground >> 11) & 0x1FU) * coverage +
                               ((background >> 11) & 0x1FU) * inverse +
                               127U) / 255U);
    uint16_t green = (uint16_t)((((foreground >> 5) & 0x3FU) * coverage +
                                 ((background >> 5) & 0x3FU) * inverse +
                                 127U) / 255U);
    uint16_t blue = (uint16_t)(((foreground & 0x1FU) * coverage +
                                (background & 0x1FU) * inverse +
                                127U) / 255U);

    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void draw_sized_text(uint16_t x, uint16_t y, uint16_t width,
                             const char *text, uint16_t foreground,
                             uint16_t background, uint8_t glyph_width,
                             uint8_t glyph_height)
{
    const uint8_t *glyphs[PY_CODE_MAX_GLYPHS];
    uint8_t source_columns[PY_FONT_W + 1U];
    uint16_t glyph_count = 0U;
    const char *cursor = text ? text : "";
    if(x >= HK_DISPLAY_REQUIRED_WIDTH || y + glyph_height > HK_DISPLAY_REQUIRED_HEIGHT)
        return;
    if(width > HK_DISPLAY_REQUIRED_WIDTH - x)
        width = HK_DISPLAY_REQUIRED_WIDTH - x;
    while(*cursor && glyph_count < width / glyph_width)
        glyphs[glyph_count++] = hk_font_glyph(utf8_next(&cursor));
    if(!glyph_count)
        return;
    const uint16_t colors[] = {
        background,
        blend_color(foreground, background, 64U),
        blend_color(foreground, background, 128U),
        blend_color(foreground, background, 191U),
        foreground,
    };
    for(uint8_t column = 0U; column <= glyph_width; ++column)
        source_columns[column] = column * HACKYLENS_FONT_W / glyph_width;
    for(uint8_t row = 0U; row < glyph_height; ++row)
    {
        uint8_t y0 = row * HACKYLENS_FONT_H / glyph_height;
        uint8_t y1 = (row + 1U) * HACKYLENS_FONT_H / glyph_height;
        uint8_t coverage_step[PY_FONT_W];
        for(uint8_t column = 0U; column < glyph_width; ++column)
            coverage_step[column] = 4U / ((y1 - y0) *
                (source_columns[column + 1U] - source_columns[column]));
        uint8_t *pixels = hk_ui_display_row_buffer();
        for(uint16_t glyph = 0U; glyph < glyph_count; ++glyph)
        {
            for(uint8_t column = 0U; column < glyph_width; ++column)
            {
                uint8_t covered = 0U;
                for(uint8_t sy = y0; sy < y1; ++sy)
                {
                    for(uint8_t sx = source_columns[column];
                        sx < source_columns[column + 1U]; ++sx)
                        if(glyphs[glyph][sy * HACKYLENS_FONT_ROW_BYTES + sx / 8U] &
                           (0x80U >> (sx & 7U)))
                            ++covered;
                }
                uint16_t color = colors[covered * coverage_step[column]];
                *pixels++ = (uint8_t)(color >> 8);
                *pixels++ = (uint8_t)color;
            }
        }
        hk_ui_display_write_row(x, (uint16_t)(y + row), glyph_count * glyph_width,
                                hk_ui_display_row_buffer());
    }
}

static void draw_small_text(uint16_t x, uint16_t y, uint16_t width,
                            const char *text, uint16_t foreground,
                            uint16_t background)
{
    draw_sized_text(x, y, width, text, foreground, background,
                    PY_FONT_W, PY_FONT_H);
}

static void draw_code_text(uint16_t x, uint16_t y, uint16_t width,
                           const char *text, uint16_t color)
{
    draw_sized_text(x, y, width, text, color, COLOR_BLACK, PY_CODE_FONT_W, PY_CODE_FONT_H);
}

uint8_t micropython_view_code_columns(void)
{
    const uint8_t columns = PY_CODE_COLUMNS < MICROPYTHON_PREVIEW_COLUMNS ?
                            PY_CODE_COLUMNS : MICROPYTHON_PREVIEW_COLUMNS;
    return columns - columns % 4U;
}

static void draw_small_centered(uint16_t y, const char *text,
                                uint16_t foreground, uint16_t background)
{
    const char *cursor = text ? text : "";
    uint16_t glyphs = 0U;
    uint16_t width;
    uint16_t x;

    while(*cursor && glyphs < PY_MAX_GLYPHS)
    {
        (void)utf8_next(&cursor);
        glyphs++;
    }
    width = (uint16_t)(glyphs * PY_FONT_W);
    x = width < HK_DISPLAY_REQUIRED_WIDTH ?
        (uint16_t)((HK_DISPLAY_REQUIRED_WIDTH - width) / 2U) : 0U;
    draw_small_text(x, y, width, text, foreground, background);
}

static const char *runtime_label(micropython_runtime_state_t state)
{
    switch(state)
    {
    case MICROPYTHON_RUNTIME_STARTING: return "STARTING";
    case MICROPYTHON_RUNTIME_RUNNING: return "RUNNING";
    case MICROPYTHON_RUNTIME_STOPPING: return "STOPPING";
    case MICROPYTHON_RUNTIME_FINISHED: return "FINISHED";
    case MICROPYTHON_RUNTIME_ERROR: return "ERROR";
    default: return "STOPPED";
    }
}

static const char *filesystem_label(userfs_state_t state)
{
    switch(state)
    {
    case USERFS_STATE_MOUNTED: return "MOUNTED";
    case USERFS_STATE_UNFORMATTED: return "UNFORMATTED";
    case USERFS_STATE_UNSUPPORTED_FLASH: return "UNSUPPORTED";
    case USERFS_STATE_CORRUPT: return "CORRUPT";
    case USERFS_STATE_IO_ERROR: return "I/O ERROR";
    default: return "OFFLINE";
    }
}

static const char *mode_title(micropython_ui_mode_t mode)
{
    switch(mode)
    {
    case MICROPYTHON_UI_ACTIONS: return "PY ACTIONS";
    case MICROPYTHON_UI_PREVIEW: return "VIEW CODE";
    case MICROPYTHON_UI_CONSOLE: return "PY LOGS";
    case MICROPYTHON_UI_DELETE_CONFIRM: return "DELETE";
    default: return "MICRO-PYTHON";
    }
}

static const char *action_label(const micropython_view_state_t *ui,
                                uint8_t index)
{
    switch(index)
    {
    case MICROPYTHON_ACTION_RUN: return "RUN";
    case MICROPYTHON_ACTION_VIEW: return "VIEW CODE";
    case MICROPYTHON_ACTION_LOGS: return "LOGS";
    case MICROPYTHON_ACTION_STARTUP:
        return ui->startup[0] &&
               strcmp(ui->startup, ui->selected_name) == 0 ?
               "CLEAR STARTUP" : "SET STARTUP";
    case MICROPYTHON_ACTION_DELETE: return "DELETE";
    case MICROPYTHON_ACTION_PIN:
        return ui->selected_pinned ? "UNPIN FROM MENU" : "PIN TO MENU";
    default: return "";
    }
}

static void fitted_text(char *output, size_t output_size, const char *text,
                        size_t columns)
{
    utf8_copy_glyphs(output, output_size, text ? text : "", columns);
}

static void format_size(char *output, size_t output_size, uint32_t bytes)
{
    if(bytes < 1024U)
        snprintf(output, output_size, "%luB", (unsigned long)bytes);
    else if(bytes < 1024U * 1024U)
        snprintf(output, output_size, "%luK", (unsigned long)(bytes / 1024U));
    else
        snprintf(output, output_size, "%luM",
                 (unsigned long)(bytes / (1024U * 1024U)));
}

static void draw_code_info(const micropython_view_state_t *ui)
{
    char position[20];
    snprintf(position, sizeof(position), "%sL%lu%s",
             ui->preview_has_previous ? "< " : "",
             (unsigned long)ui->preview_line,
             ui->preview_has_next ? " >" : "");
    uint16_t width = (uint16_t)(strlen(position) * PY_CODE_FONT_W);
    uint16_t right = HK_DISPLAY_REQUIRED_WIDTH - PY_CODE_MARGIN - width;
    draw_code_text(PY_CODE_MARGIN, 34U, right - PY_CODE_MARGIN * 2U,
                   ui->selected_name, COLOR_WHITE);
    draw_code_text(right, 34U, width, position, hk_ui_accent());
}

static void draw_info(const micropython_view_state_t *ui,
                      const micropython_runtime_status_t *runtime,
                      const userfs_status_t *filesystem)
{
    char line[96];
    char clipped[26];

    hk_ui_display_fill_rect(0U, PY_INFO_Y, HK_DISPLAY_REQUIRED_WIDTH,
                            PY_INFO_H, COLOR_BLACK);
    if(ui->status[0])
    {
        fitted_text(line, sizeof(line), ui->status,
                    MICROPYTHON_STATUS_COLUMNS);
    }
    else if(filesystem->state != USERFS_STATE_MOUNTED &&
       ui->mode != MICROPYTHON_UI_CONSOLE)
    {
        snprintf(line, sizeof(line), "USERFS %s",
                 filesystem_label(filesystem->state));
    }
    else if(ui->mode == MICROPYTHON_UI_LIST)
    {
        uint32_t free_bytes = filesystem->total_bytes > filesystem->used_bytes ?
                              filesystem->total_bytes - filesystem->used_bytes : 0U;
        char free_size[12];
        format_size(free_size, sizeof(free_size), free_bytes);
        snprintf(line, sizeof(line), "%lu PY   %s FREE   STARTUP %s",
                 (unsigned long)ui->file_count, free_size,
                 ui->startup[0] ? "ON" : "OFF");
    }
    else if(ui->mode == MICROPYTHON_UI_CONSOLE)
    {
        snprintf(line, sizeof(line), "RUN %lu  %s  %lu/%lu",
                 (unsigned long)runtime->run_id, runtime_label(runtime->state),
                 (unsigned long)(ui->log_top + (ui->log_count ? 1U : 0U)),
                 (unsigned long)ui->log_total);
    }
    else if(ui->mode == MICROPYTHON_UI_PREVIEW)
    {
        draw_code_info(ui);
        return;
    }
    else
    {
        fitted_text(line, sizeof(line), ui->selected_name, PY_TEXT_COLUMNS);
    }
    fitted_text(clipped, sizeof(clipped), line, PY_TEXT_COLUMNS);
    draw_small_text(PY_TEXT_X, 31U,
                    HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U, clipped,
                    hk_ui_accent(), COLOR_BLACK);
}

static void draw_list_row(const micropython_view_state_t *ui, uint8_t row)
{
    uint16_t y = (uint16_t)(PY_BODY_Y + row * PY_LIST_ROW_PITCH);
    uint8_t selected = row < ui->visible_count &&
                       ui->list_top + row == ui->selected_index;
    uint16_t fg = selected ? COLOR_BLACK : hk_ui_accent();
    uint16_t bg = selected ? hk_ui_accent() : COLOR_BLACK;
    char name[USERFS_NAME_MAX + 1U];
    char size[12];
    char line[96];
    char clipped[26];

    hk_ui_display_fill_rect(6U, y, HK_DISPLAY_REQUIRED_WIDTH - 12U,
                            PY_LIST_ROW_H, bg);
    if(row >= ui->visible_count)
        return;
    fitted_text(name, sizeof(name), ui->files[row].name, 14U);
    format_size(size, sizeof(size), ui->files[row].size);
    snprintf(line, sizeof(line), "%c %-14s %8s",
             ui->files[row].startup ? '*' : ' ', name, size);
    fitted_text(clipped, sizeof(clipped), line, PY_TEXT_COLUMNS);
    draw_small_text(PY_TEXT_X, (uint16_t)(y + 2U),
                    HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U,
                    clipped, fg, bg);
}

static void draw_action_row(const micropython_view_state_t *ui, uint8_t row)
{
    uint16_t y = (uint16_t)(PY_ACTION_ROW_Y0 + row * PY_ACTION_ROW_PITCH);
    uint8_t selected = row == ui->action_index;
    uint16_t fg = selected ? COLOR_BLACK : hk_ui_accent();
    uint16_t bg = selected ? hk_ui_accent() : COLOR_BLACK;

    hk_ui_display_fill_rect(20U, y, HK_DISPLAY_REQUIRED_WIDTH - 40U,
                            PY_ACTION_ROW_H, bg);
    draw_small_text(28U, (uint16_t)(y + 3U), HK_DISPLAY_REQUIRED_WIDTH - 56U,
                    action_label(ui, row), fg, bg);
}

static void draw_body(const micropython_view_state_t *ui,
                      const userfs_status_t *filesystem)
{
    char line[48];

    hk_ui_display_fill_rect(0U, PY_BODY_Y, HK_DISPLAY_REQUIRED_WIDTH,
                            PY_BODY_H, COLOR_BLACK);
    if(filesystem->state != USERFS_STATE_MOUNTED &&
       ui->mode != MICROPYTHON_UI_CONSOLE)
    {
        draw_small_centered(94U, "STORAGE NOT READY",
                            hk_ui_accent(), COLOR_BLACK);
        return;
    }
    if(ui->mode == MICROPYTHON_UI_LIST)
    {
        if(!ui->file_count)
        {
            draw_small_centered(84U, "NO PYTHON FILES",
                                hk_ui_accent(), COLOR_BLACK);
            draw_small_centered(112U, "UPLOAD .PY FROM PC",
                                COLOR_WHITE, COLOR_BLACK);
            draw_small_centered(140U, "WITH HKPY TOOL",
                                COLOR_WHITE, COLOR_BLACK);
            return;
        }
        for(uint8_t row = 0U; row < MICROPYTHON_LIST_VISIBLE_ROWS; row++)
            draw_list_row(ui, row);
        return;
    }
    if(ui->mode == MICROPYTHON_UI_ACTIONS)
    {
        for(uint8_t row = 0U; row < MICROPYTHON_ACTION_COUNT; row++)
            draw_action_row(ui, row);
        return;
    }
    if(ui->mode == MICROPYTHON_UI_PREVIEW)
    {
        uint16_t gutter = blend_color(hk_ui_accent(), COLOR_BLACK, 155U);
        hk_ui_display_fill_rect(PY_CODE_X - 2U, PY_BODY_Y, 1U, PY_BODY_H, gutter);
        for(uint8_t row = 0U; row < ui->preview_count; row++)
        {
            uint16_t y = (uint16_t)(PY_BODY_Y + row * PY_CODE_ROW_PITCH);
            if(ui->preview_line_number[row])
                snprintf(line, sizeof(line), "%6lu",
                         (unsigned long)ui->preview_line_number[row]);
            else
                memcpy(line, "     >", sizeof("     >"));
            draw_code_text(PY_CODE_MARGIN, y, PY_CODE_NUMBER_COLS * PY_CODE_FONT_W,
                           line, gutter);
            draw_code_text(PY_CODE_X, y,
                           micropython_view_code_columns() * PY_CODE_FONT_W,
                           ui->preview[row], COLOR_WHITE);
        }
        return;
    }
    if(ui->mode == MICROPYTHON_UI_CONSOLE)
    {
        for(uint8_t row = 0U; row < ui->log_count; row++)
            draw_small_text(
                PY_TEXT_X, (uint16_t)(PY_BODY_Y + row * PY_TEXT_ROW_PITCH),
                HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U,
                ui->log_lines[row], hk_ui_accent(), COLOR_BLACK);
        return;
    }
    draw_small_centered(76U, "DELETE THIS SCRIPT?",
                        COLOR_WHITE, COLOR_BLACK);
    fitted_text(line, sizeof(line), ui->selected_name, PY_TEXT_COLUMNS);
    draw_small_centered(108U, line, hk_ui_accent(), COLOR_BLACK);
    if(ui->startup[0] && strcmp(ui->startup, ui->selected_name) == 0)
        draw_small_centered(140U, "STARTUP WILL BE CLEARED",
                            COLOR_WHITE, COLOR_BLACK);
}

static void add_region(hk_ui_display_rect_t *regions, uint16_t *count,
                       uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    if(*count >= HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS)
        return;
    regions[*count] = (hk_ui_display_rect_t){x, y, width, height};
    (*count)++;
}

void micropython_view_render(
    const micropython_view_state_t *ui,
    const micropython_runtime_status_t *runtime,
    const userfs_status_t *filesystem,
    const micropython_view_update_t *update)
{
    hk_ui_display_surface_t frame;
    hk_ui_display_rect_t regions[HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS];
    uint16_t region_count = 0U;

    if(!ui || !runtime || !filesystem || !update ||
       !hk_ui_display_frame_acquire(&frame))
        return;
    if(update->full)
    {
        menu_draw_chrome(mode_title(ui->mode));
        draw_info(ui, runtime, filesystem);
        draw_body(ui, filesystem);
        if(!hk_ui_display_frame_present(frame.lease_id))
            hk_ui_display_frame_cancel(frame.lease_id);
        return;
    }
    if(update->info)
    {
        draw_info(ui, runtime, filesystem);
        add_region(regions, &region_count, 0U, PY_INFO_Y,
                   HK_DISPLAY_REQUIRED_WIDTH, PY_INFO_H);
    }
    if(update->body)
    {
        draw_body(ui, filesystem);
        add_region(regions, &region_count, 0U, PY_BODY_Y,
                   HK_DISPLAY_REQUIRED_WIDTH, PY_BODY_H);
    }
    else if(update->row_mask)
    {
        uint8_t count = ui->mode == MICROPYTHON_UI_ACTIONS ?
                        MICROPYTHON_ACTION_COUNT :
                        MICROPYTHON_LIST_VISIBLE_ROWS;
        for(uint8_t row = 0U; row < count; row++)
        {
            if(!(update->row_mask & (uint8_t)(1U << row)))
                continue;
            if(ui->mode == MICROPYTHON_UI_ACTIONS)
            {
                draw_action_row(ui, row);
                add_region(regions, &region_count, 20U,
                           (uint16_t)(PY_ACTION_ROW_Y0 +
                                      row * PY_ACTION_ROW_PITCH),
                           HK_DISPLAY_REQUIRED_WIDTH - 40U, PY_ACTION_ROW_H);
            }
            else if(ui->mode == MICROPYTHON_UI_LIST)
            {
                draw_list_row(ui, row);
                add_region(regions, &region_count, 6U,
                           (uint16_t)(PY_BODY_Y + row * PY_LIST_ROW_PITCH),
                           HK_DISPLAY_REQUIRED_WIDTH - 12U, PY_LIST_ROW_H);
            }
        }
    }
    if(!region_count)
    {
        hk_ui_display_frame_cancel(frame.lease_id);
        return;
    }
    if(!hk_ui_display_frame_present_regions(frame.lease_id, regions,
                                             region_count))
        hk_ui_display_frame_cancel(frame.lease_id);
}

void micropython_view_draw_icon(uint16_t x, uint16_t y,
                                uint16_t color, uint16_t background)
{
    hk_menu_icon_draw(x, y, color, background, hk_menu_icon_micropython);
}
