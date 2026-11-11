#include "files_view.h"

#include <stdio.h>
#include <string.h>

#include "files_types.h"

#include "files_layout.h"

#include "files_view_port.h"
#include "files_firmware.h"

static hk_ui_display_surface_t g_animation_surface;
static hk_indexed_frame_t g_animation_previous_frame;
static hk_indexed_frame_t g_animation_current_frame;
static uint16_t g_animation_canvas_w;
static uint16_t g_animation_canvas_h;
static uint16_t g_animation_view_x;
static uint16_t g_animation_view_y;
static uint16_t g_animation_view_w;
static uint16_t g_animation_view_h;
static uint16_t g_animation_background;
static uint16_t g_animation_source_x[HK_DISPLAY_REQUIRED_WIDTH];
static uint16_t g_animation_source_y[HK_DISPLAY_REQUIRED_HEIGHT];
static uint8_t *g_animation_backup;
static frame_workspace_borrow_t g_animation_workspace;
static uint8_t g_animation_frame_open;
static uint8_t g_animation_reset_pending;
static uint8_t g_animation_previous_valid;
static uint8_t g_animation_backup_valid;
static uint32_t g_image_lease;

static void files_view_enter_impl(void)
{
}

static void files_view_draw_status_impl(const char *line)
{
    menu_draw_chrome("FILES");
    hk_ui_display_draw_text_centered(96, line, COLOR_TERM_GREEN, COLOR_BLACK);
}

static void files_view_draw_file_status_impl(const char *name, const char *status)
{
    hk_ui_display_surface_t surface;
    if(!hk_ui_display_frame_acquire(&surface))
        return;
    menu_draw_chrome("FILES");
    hk_ui_display_draw_text_clipped_at(10, 91, HK_DISPLAY_REQUIRED_WIDTH - 20,
                                       name, COLOR_TERM_GREEN, COLOR_BLACK);
    hk_ui_display_draw_text_at(10, 121, status, COLOR_TERM_GREEN, COLOR_BLACK);
    (void)hk_ui_display_frame_present(surface.lease_id);
}

static void files_view_draw_item_icon(file_item_kind_t kind, uint16_t x, uint16_t y,
                                      uint16_t fg, uint16_t bg)
{
    if(kind == FILE_ITEM_DIRECTORY)
    {
        hk_ui_display_draw_rect(x, y + 4U, 15U, 10U, 1U, fg);
        hk_ui_display_fill_rect(x, y + 2U, 6U, 2U, fg);
        return;
    }

    hk_ui_display_draw_rect(x + 2U, y, 11U, 15U, 1U, fg);
    hk_ui_display_fill_rect(x + 10U, y, 3U, 3U, bg);
    hk_ui_display_fill_rect(x + 10U, y + 1U, 1U, 3U, fg);
    hk_ui_display_fill_rect(x + 10U, y + 3U, 3U, 1U, fg);
    hk_ui_display_fill_rect(x + 5U, y + 7U, 5U, 1U, fg);
    hk_ui_display_fill_rect(x + 5U, y + 10U, 5U, 1U, fg);
}

static void files_view_draw_row_impl(uint8_t row, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    uint8_t entry_index = (uint8_t)(top + row);
    uint16_t y = FILES_ROW_Y0 + row * FILES_ROW_H;
    uint8_t selected = entry_index == index;
    uint16_t fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    uint16_t bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
    hk_ui_display_fill_rect(6, y, HK_DISPLAY_REQUIRED_WIDTH - 12, FILES_ROW_H - 2, bg);
    if(entry_index >= count)
        return;

    files_view_draw_item_icon(items[entry_index].kind, 11U, y + 5U, fg, bg);
    hk_ui_display_draw_text_clipped_at(33U, y + 1U, HK_DISPLAY_REQUIRED_WIDTH - 42U,
                                       items[entry_index].name, fg, bg);
}

static void files_view_update_row_impl(uint8_t row, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    hk_ui_display_surface_t surface;
    if(!hk_ui_display_frame_acquire(&surface))
        return;
    files_view_draw_row_impl(row, items, count, top, index);
    const hk_ui_display_rect_t dirty = {6, FILES_ROW_Y0 + row * FILES_ROW_H,
                                       HK_DISPLAY_REQUIRED_WIDTH - 12, FILES_ROW_H - 2};
    (void)hk_ui_display_frame_present_regions(surface.lease_id, &dirty, 1U);
}

static void files_view_render_list_impl(uint8_t sd_present, uint8_t fat_mounted, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    hk_ui_display_surface_t surface;
    if(!hk_ui_display_frame_acquire(&surface))
        return;
    menu_draw_chrome("FILES");
    if(!sd_present)
        hk_ui_display_draw_text_centered(96, "NO SD", COLOR_TERM_GREEN, COLOR_BLACK);
    else if(!fat_mounted)
        hk_ui_display_draw_text_centered(96, "FAT32 ONLY", COLOR_TERM_GREEN, COLOR_BLACK);
    else if(count == 0)
        hk_ui_display_draw_text_centered(96, "EMPTY", COLOR_TERM_GREEN, COLOR_BLACK);
    else
        for(uint8_t row = 0; row < FILES_ROWS; row++)
            files_view_draw_row_impl(row, items, count, top, index);
    (void)hk_ui_display_frame_present(surface.lease_id);
}

static void files_view_render_preview_impl(const char *preview, uint16_t len, uint8_t page)
{
    uint16_t chars_per_page = FILE_PREVIEW_ROWS * TERM_COLS;
    uint16_t start = (uint16_t)page * chars_per_page;

    menu_draw_chrome("PREVIEW");
    if(len == 0)
    {
        hk_ui_display_draw_text_centered(96, "EMPTY", COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }

    for(uint8_t row = 0; row < FILE_PREVIEW_ROWS; row++)
    {
        char line[TERM_COLS + 1];
        uint16_t offset = start + row * TERM_COLS;
        memset(line, ' ', TERM_COLS);
        line[TERM_COLS] = '\0';
        for(uint8_t col = 0; col < TERM_COLS && offset + col < len; col++)
            line[col] = preview[offset + col];
        hk_ui_display_draw_text_at(0, FILES_ROW_Y0 + row * HACKYLENS_FONT_H, line, COLOR_TERM_GREEN, COLOR_BLACK);
    }
}

static uint8_t files_view_begin_image_impl(void)
{
    hk_ui_display_surface_t surface;
    if(!hk_ui_display_frame_acquire(&surface))
        return 0U;
    g_image_lease = surface.lease_id;
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
    return 1U;
}

static uint8_t files_view_end_image_impl(uint8_t present)
{
    uint32_t lease = g_image_lease;
    g_image_lease = 0U;
    if(!lease)
        return 1U;
    if(present)
        return hk_ui_display_frame_present(lease);
    hk_ui_display_frame_cancel(lease);
    return 1U;
}

static void files_view_scale_row(uint8_t *pixels, uint16_t dst_w,
                                 const uint8_t *row, uint16_t src_w,
                                 uint8_t bpp, uint8_t bgr_order)
{
    uint8_t bytes = bpp == 16U ? 2U : 3U;
    for(uint16_t x = 0; x < dst_w; x++)
    {
        uint32_t source_x = (uint32_t)x * src_w / dst_w;
        const uint8_t *pixel = row + source_x * bytes;
        uint16_t color;
        if(bytes == 2U)
            color = (uint16_t)pixel[0] | ((uint16_t)pixel[1] << 8);
        else
        {
            uint8_t red = bgr_order ? pixel[2] : pixel[0];
            uint8_t blue = bgr_order ? pixel[0] : pixel[2];
            color = (uint16_t)(((uint16_t)(red & 0xf8U) << 8) |
                              ((uint16_t)(pixel[1] & 0xfcU) << 3) |
                              (uint16_t)(blue >> 3));
        }
        pixels[x * 2U] = (uint8_t)(color >> 8);
        pixels[x * 2U + 1U] = (uint8_t)color;
    }
}

static void files_view_render_image_row_span_impl(uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order)
{
    uint16_t dst_x;
    uint16_t dst_y;
    uint16_t dst_w;
    uint16_t dst_h;
    uint16_t y0;
    uint16_t y1;

    image_fit_viewport(src_w, (uint16_t)src_h, &dst_x, &dst_y, &dst_w, &dst_h);
    y0 = (uint16_t)(dst_y + src_y * dst_h / src_h);
    y1 = (uint16_t)(dst_y + ((src_y + 1U) * dst_h + src_h - 1U) / src_h);

    if(y1 <= y0)
        y1 = (uint16_t)(y0 + 1U);
    if(y1 > dst_y + dst_h)
        y1 = (uint16_t)(dst_y + dst_h);

    uint8_t *pixels = hk_ui_display_row_buffer();
    files_view_scale_row(pixels, dst_w, row, src_w, bpp, bgr_order);
    for(uint16_t y = y0; y < y1; y++)
        hk_ui_display_write_row(dst_x, y, dst_w, pixels);
}

static void files_view_surface_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t *pixel;

    if(x >= g_animation_surface.width || y >= g_animation_surface.height)
        return;
    pixel = g_animation_surface.rgb565_be +
            (uint32_t)y * g_animation_surface.stride_bytes + (uint32_t)x * 2U;
    pixel[0] = (uint8_t)(color >> 8);
    pixel[1] = (uint8_t)color;
}

static uint8_t files_view_source_inside(uint16_t dst_x, uint16_t dst_y,
                                        uint16_t src_x, uint16_t src_y,
                                        uint16_t src_w, uint16_t src_h)
{
    uint16_t logical_x = g_animation_source_x[dst_x];
    uint16_t logical_y = g_animation_source_y[dst_y];
    return logical_x >= src_x && logical_x < (uint32_t)src_x + src_w &&
           logical_y >= src_y && logical_y < (uint32_t)src_y + src_h;
}

static void files_view_fill_logical_rect(uint16_t src_x, uint16_t src_y,
                                         uint16_t src_w, uint16_t src_h,
                                         uint16_t color)
{
    for(uint16_t y = 0; y < g_animation_view_h; y++)
    {
        for(uint16_t x = 0; x < g_animation_view_w; x++)
        {
            if(files_view_source_inside(x, y, src_x, src_y, src_w, src_h))
                files_view_surface_pixel((uint16_t)(g_animation_view_x + x),
                                         (uint16_t)(g_animation_view_y + y), color);
        }
    }
}

static void files_view_reset_animation_surface(void)
{
    memset(g_animation_surface.rgb565_be, 0,
           (uint32_t)g_animation_surface.stride_bytes * g_animation_surface.height);
    files_view_fill_logical_rect(0, 0, g_animation_canvas_w, g_animation_canvas_h,
                                 g_animation_background);
    g_animation_reset_pending = 0;
    g_animation_previous_valid = 0;
    g_animation_backup_valid = 0;
}

static void files_view_apply_previous_disposal(void)
{
    if(!g_animation_previous_valid)
        return;
    if(g_animation_previous_frame.disposal == 2U)
    {
        files_view_fill_logical_rect(g_animation_previous_frame.frame_x,
                                     g_animation_previous_frame.frame_y,
                                     g_animation_previous_frame.frame_w,
                                     g_animation_previous_frame.frame_h,
                                     g_animation_background);
    }
    else if(g_animation_previous_frame.disposal == 3U &&
            g_animation_backup_valid && g_animation_backup)
    {
        memcpy(g_animation_surface.rgb565_be, g_animation_backup,
               (uint32_t)g_animation_surface.stride_bytes * g_animation_surface.height);
    }
}

static uint8_t files_view_animation_begin_impl(uint16_t canvas_w, uint16_t canvas_h,
                                               uint16_t background_rgb565)
{
    if(canvas_w == 0 || canvas_h == 0)
        return 0;
    if(g_animation_frame_open)
    {
        hk_ui_display_frame_cancel(g_animation_surface.lease_id);
        g_animation_frame_open = 0;
    }
    g_animation_canvas_w = canvas_w;
    g_animation_canvas_h = canvas_h;
    g_animation_background = background_rgb565;
    image_fit_viewport(canvas_w, canvas_h, &g_animation_view_x, &g_animation_view_y,
                       &g_animation_view_w, &g_animation_view_h);
    for(uint16_t x = 0; x < g_animation_view_w; x++)
        g_animation_source_x[x] = (uint16_t)((uint32_t)x * canvas_w / g_animation_view_w);
    for(uint16_t y = 0; y < g_animation_view_h; y++)
        g_animation_source_y[y] = (uint16_t)((uint32_t)y * canvas_h / g_animation_view_h);
    if(g_animation_workspace.generation != 0U)
        (void)frame_workspace_release(&g_animation_workspace);
    if(!frame_workspace_borrow(
            HK_DISPLAY_REQUIRED_WIDTH * HK_DISPLAY_REQUIRED_HEIGHT * 2U,
            &g_animation_workspace))
        return 0;
    g_animation_backup = g_animation_workspace.data;
    g_animation_reset_pending = 1;
    g_animation_previous_valid = 0;
    g_animation_backup_valid = 0;
    return g_animation_backup != NULL;
}

static uint8_t files_view_animation_frame_begin_impl(const hk_indexed_frame_t *frame)
{
    uint32_t surface_bytes;

    if(!frame || frame->canvas_w != g_animation_canvas_w ||
       frame->canvas_h != g_animation_canvas_h || g_animation_frame_open ||
       !hk_ui_display_frame_acquire(&g_animation_surface))
        return 0;
    g_animation_frame_open = 1;
    surface_bytes = (uint32_t)g_animation_surface.stride_bytes * g_animation_surface.height;
    if(g_animation_reset_pending)
        files_view_reset_animation_surface();
    else
        files_view_apply_previous_disposal();

    g_animation_current_frame = *frame;
    if(frame->disposal == 3U)
    {
        memcpy(g_animation_backup, g_animation_surface.rgb565_be, surface_bytes);
        g_animation_backup_valid = 1;
    }
    else
    {
        g_animation_backup_valid = 0;
    }
    return 1;
}

static void files_view_animation_render_indexed_row_impl(const hk_indexed_frame_t *frame,
                                                         uint16_t frame_row,
                                                         const uint8_t *indices,
                                                         const uint16_t *palette,
                                                         uint16_t palette_size)
{
    uint16_t logical_y;

    if(!g_animation_frame_open || !frame || !indices || !palette ||
       frame_row >= frame->frame_h)
        return;
    logical_y = (uint16_t)(frame->frame_y + frame_row);
    for(uint16_t y = 0; y < g_animation_view_h; y++)
    {
        uint16_t source_y = g_animation_source_y[y];
        if(source_y != logical_y)
            continue;
        for(uint16_t x = 0; x < g_animation_view_w; x++)
        {
            uint16_t source_x = g_animation_source_x[x];
            uint16_t local_x;
            uint8_t index;

            if(source_x < frame->frame_x || source_x >= (uint32_t)frame->frame_x + frame->frame_w)
                continue;
            local_x = (uint16_t)(source_x - frame->frame_x);
            index = indices[local_x];
            if(index >= palette_size || index == frame->transparent_index)
                continue;
            files_view_surface_pixel((uint16_t)(g_animation_view_x + x),
                                     (uint16_t)(g_animation_view_y + y), palette[index]);
        }
    }
}

static uint8_t files_view_animation_frame_end_impl(void)
{
    uint32_t lease_id;

    if(!g_animation_frame_open)
        return 0;
    lease_id = g_animation_surface.lease_id;
    g_animation_frame_open = 0;
    g_animation_previous_frame = g_animation_current_frame;
    g_animation_previous_valid = 1;
    if(!hk_ui_display_frame_present(lease_id))
    {
        hk_ui_display_frame_cancel(lease_id);
        return 0;
    }
    return 1;
}

static void files_view_animation_end_impl(void)
{
    if(g_animation_frame_open)
        hk_ui_display_frame_cancel(g_animation_surface.lease_id);
    memset(&g_animation_surface, 0, sizeof(g_animation_surface));
    memset(&g_animation_previous_frame, 0, sizeof(g_animation_previous_frame));
    memset(&g_animation_current_frame, 0, sizeof(g_animation_current_frame));
    g_animation_frame_open = 0;
    g_animation_reset_pending = 0;
    g_animation_previous_valid = 0;
    g_animation_backup_valid = 0;
    g_animation_backup = NULL;
    if(g_animation_workspace.generation != 0U)
        (void)frame_workspace_release(&g_animation_workspace);
}

static void files_view_draw_delete_confirm_impl(const char *name)
{
    menu_draw_chrome("DELETE");
    hk_ui_display_draw_text_centered(68, "do u want delete?", COLOR_TERM_GREEN, COLOR_BLACK);
    hk_ui_display_draw_text_centered(104, name, COLOR_TERM_GREEN, COLOR_BLACK);
    hk_ui_display_draw_text_centered(152, "OK DELETE", COLOR_TERM_GREEN, COLOR_BLACK);
    hk_ui_display_draw_text_centered(182, "BACK CANCEL", COLOR_TERM_GREEN, COLOR_BLACK);
}

void files_view_init(void)
{
    static const files_view_ops_t ops = {
        .enter = files_view_enter_impl,
        .draw_status = files_view_draw_status_impl,
        .draw_file_status = files_view_draw_file_status_impl,
        .draw_row = files_view_update_row_impl,
        .render_list = files_view_render_list_impl,
        .render_preview = files_view_render_preview_impl,
        .begin_image = files_view_begin_image_impl,
        .end_image = files_view_end_image_impl,
        .render_image_row_span = files_view_render_image_row_span_impl,
        .animation_begin = files_view_animation_begin_impl,
        .animation_frame_begin = files_view_animation_frame_begin_impl,
        .animation_render_indexed_row = files_view_animation_render_indexed_row_impl,
        .animation_frame_end = files_view_animation_frame_end_impl,
        .animation_end = files_view_animation_end_impl,
        .draw_delete_confirm = files_view_draw_delete_confirm_impl,
    };

    files_view_register(&ops);
}
