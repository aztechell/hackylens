#include <assert.h>
#include <string.h>

#include "../firmware/src/apps/files/files_actions.h"
#include "../firmware/src/apps/files/file_browser_list.h"
#include "../firmware/src/apps/files/file_browser_mode.h"
#include "../firmware/src/apps/files/file_preview.h"
#include "../firmware/src/apps/files/files_presenter.h"

static const fat_file_entry_t entries[] = {
    {.name = "FIRST.BMP"},
    {.name = "INVALID.PNG"},
    {.name = "NOTES.TXT"},
    {.name = "LAST.RAW"},
};
static uint8_t selected;
static const fat_file_entry_t *displayed;
static file_browser_mode_t mode = FILES_MODE_IMAGE;

const fat_file_entry_t *files_entry_at(uint8_t index) { return &entries[index]; }
uint8_t files_count(void) { return sizeof(entries) / sizeof(entries[0]); }
uint8_t files_index(void) { return selected; }
uint8_t files_top(void) { return 0U; }
void files_set_index(uint8_t index) { selected = index; }
void files_ensure_visible(void) {}
file_browser_mode_t files_mode(void) { return mode; }
void files_set_mode(file_browser_mode_t value) { mode = value; }
uint8_t files_preview_page(void) { return 0U; }
uint16_t files_preview_len(void) { return 0U; }
void files_set_preview_page(uint8_t page) { (void)page; }
void files_presenter_render_preview(void) {}
void files_presenter_render_list(void) {}
void files_presenter_draw_row(uint8_t row) { (void)row; }
uint8_t files_entry_is_image(const fat_file_entry_t *entry) { return entry != &entries[2]; }

file_result_t files_presenter_render_image(const fat_file_entry_t *entry)
{
    assert(mode == FILES_MODE_IMAGE && entry == &entries[selected]);
    displayed = entry;
    return entry == &entries[1] ? FILE_RESULT_UNSUPPORTED_FORMAT : FILE_RESULT_OK;
}

int main(void)
{
    files_nav_delta(1);
    assert(selected == 1U && displayed == &entries[1]);
    files_nav_delta(1);
    assert(selected == 3U && displayed == &entries[3]);
    files_nav_delta(-1);
    assert(selected == 1U && displayed == &entries[1]);
    files_nav_delta(-1);
    assert(selected == 0U && displayed == &entries[0]);
    return 0;
}
