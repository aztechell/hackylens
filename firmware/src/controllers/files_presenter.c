#include "files_presenter.h"

#include <stddef.h>

#include "../core/files_view_port.h"
#include "../storage/file_browser_list.h"
#include "../storage/file_browser_mode.h"
#include "../storage/fat32_volume.h"
#include "../storage/image_viewer.h"
#include "../storage/file_preview.h"

static const files_view_ops_t *g_files_view_ops;

void files_view_register(const files_view_ops_t *ops)
{
    g_files_view_ops = ops;
}

static void files_presenter_draw_status(const char *line)
{
    if(g_files_view_ops && g_files_view_ops->draw_status)
        g_files_view_ops->draw_status(line);
}

void files_presenter_show_status(const char *line)
{
    files_presenter_draw_status(line);
}

void files_presenter_enter(void)
{
    if(g_files_view_ops && g_files_view_ops->enter)
        g_files_view_ops->enter();
}

void files_presenter_draw_row(uint8_t row)
{
    if(g_files_view_ops && g_files_view_ops->draw_row)
        g_files_view_ops->draw_row(row, files_list_items(), files_count(), files_top(), files_index());
}

void files_presenter_render_list(void)
{
    if(files_count())
        files_ensure_visible();
    if(g_files_view_ops && g_files_view_ops->render_list)
        g_files_view_ops->render_list(hk_sd_present(), hk_fat_mounted(), files_list_items(), files_count(), files_top(), files_index());
}

void files_presenter_render_preview(void)
{
    if(g_files_view_ops && g_files_view_ops->render_preview)
        g_files_view_ops->render_preview(files_preview_text(), files_preview_len(), files_preview_page());
}

static void files_presenter_begin_image(void *context)
{
    (void)context;
    if(g_files_view_ops && g_files_view_ops->clear_image)
        g_files_view_ops->clear_image();
}

void files_presenter_draw_delete_confirm(const char *name)
{
    if(g_files_view_ops && g_files_view_ops->draw_delete_confirm)
        g_files_view_ops->draw_delete_confirm(name);
}

static void files_presenter_render_image_row(void *context, uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order)
{
    (void)context;
    if(g_files_view_ops && g_files_view_ops->render_image_row_span)
        g_files_view_ops->render_image_row_span(src_y, src_h, row, src_w, bpp, bgr_order);
}

void files_presenter_show_result(file_result_t result)
{
    if(result == FILE_RESULT_READ_ERROR)
        files_presenter_draw_status("READ ERR");
    else if(result == FILE_RESULT_UNSUPPORTED_FORMAT)
        files_presenter_draw_status("UNSUPPORTED");
    else if(result == FILE_RESULT_TOO_LARGE)
        files_presenter_draw_status("TOO LARGE");
    else if(result == FILE_RESULT_DELETE_FAILED)
        files_presenter_draw_status("DELETE FAIL");
    else if(result == FILE_RESULT_NOT_FOUND)
        files_presenter_draw_status("NO FILE");
}

file_result_t files_presenter_render_image(const fat_file_entry_t *entry)
{
    static const file_image_sink_t sink = {
        .begin = files_presenter_begin_image,
        .render_row_span = files_presenter_render_image_row,
        .context = NULL,
    };
    file_result_t result = files_open_image_entry(entry, &sink);

    if(result != FILE_RESULT_OK)
        files_presenter_show_result(result);
    return result;
}
