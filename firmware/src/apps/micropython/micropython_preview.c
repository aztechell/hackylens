#include "micropython_preview.h"

#include <string.h>

enum { TAB_WIDTH = 4U, CHECKPOINT_BYTES = 1024U };

typedef struct
{
    uint32_t offset;
    uint32_t line;
    uint8_t continuation;
} code_position_t;

typedef struct
{
    char text[MICROPYTHON_PREVIEW_LINES][MICROPYTHON_PREVIEW_LINE_BYTES];
    uint32_t line_number[MICROPYTHON_PREVIEW_LINES];
    uint8_t count;
} code_page_t;

static struct
{
    uint32_t size;
    uint32_t buffer_start;
    size_t buffer_size;
    uint8_t buffer[MICROPYTHON_PREVIEW_READ_CHUNK];
    uint8_t columns;
    code_position_t current;
    code_position_t next;
    code_position_t checkpoints[USERFS_FILE_MAX / CHECKPOINT_BYTES + 1U];
} g_code;

/* Callers only peek within the size returned by userfs_stat(). */
static userfs_result_t peek(const char *name, uint32_t offset, uint8_t *value)
{
    if(offset - g_code.buffer_start >= g_code.buffer_size)
    {
        g_code.buffer_size = 0U;
        userfs_result_t result = userfs_read(
            name, offset, g_code.buffer, sizeof(g_code.buffer), &g_code.buffer_size);
        if(result != USERFS_OK)
            return result;
        if(!g_code.buffer_size)
            return USERFS_ERROR_IO;
        g_code.buffer_start = offset;
    }
    *value = g_code.buffer[offset - g_code.buffer_start];
    return USERFS_OK;
}

static userfs_result_t read_glyph(const char *name, code_position_t *position,
                                  uint8_t first, char *output, size_t *written)
{
    char glyph[4] = {(char)first};
    size_t length = 1U;
    size_t expected = 1U;

    if(first >= 0xc2U && first <= 0xf4U)
        expected = first < 0xe0U ? 2U : first < 0xf0U ? 3U : 4U;
    while(length < expected && position->offset < g_code.size)
    {
        uint8_t next;
        userfs_result_t result = peek(name, position->offset, &next);
        if(result != USERFS_OK)
            return result;
        if((next & 0xc0U) != 0x80U)
            break;
        glyph[length++] = (char)next;
        ++position->offset;
    }
    if(length != expected || (expected == 1U && (first < 32U || first >= 127U)))
    {
        glyph[0] = '?';
        length = 1U;
    }
    if(output)
        memcpy(output + *written, glyph, length);
    *written += length;
    return USERFS_OK;
}

static userfs_result_t consume_break(const char *name, code_position_t *position,
                                     uint8_t value)
{
    ++position->offset;
    if(value == '\r' && position->offset < g_code.size)
    {
        uint8_t next;
        userfs_result_t result = peek(name, position->offset, &next);
        if(result != USERFS_OK)
            return result;
        if(next == '\n')
            ++position->offset;
    }
    ++position->line;
    position->continuation = 0U;
    return USERFS_OK;
}

static userfs_result_t read_row(const char *name, code_position_t *position,
                                char *output)
{
    size_t bytes = 0U;
    uint8_t column = 0U;
    if(output)
        output[0] = '\0';
    while(position->offset < g_code.size)
    {
        uint8_t value;
        userfs_result_t result = peek(name, position->offset, &value);
        if(result != USERFS_OK)
            return result;
        if(value == '\n' || value == '\r')
            return consume_break(name, position, value);
        if(column == g_code.columns)
        {
            position->continuation = 1U;
            break;
        }
        ++position->offset;
        if(value == '\t')
        {
            uint8_t spaces = TAB_WIDTH - column % TAB_WIDTH;
            if(output)
                memset(output + bytes, ' ', spaces);
            bytes += spaces;
            column += spaces;
        }
        else
        {
            result = read_glyph(name, position, value, output, &bytes);
            if(result != USERFS_OK)
                return result;
            ++column;
        }
        if(output)
            output[bytes] = '\0';
    }
    return USERFS_OK;
}

static userfs_result_t read_page(const char *name, code_position_t *position,
                                 code_page_t *page)
{
    uint8_t row = 0U;
    for(; row < MICROPYTHON_PREVIEW_LINES && position->offset < g_code.size; ++row)
    {
        char *text = NULL;
        if(page)
        {
            text = page->text[row];
            page->line_number[row] = position->continuation ? 0U : position->line;
        }
        userfs_result_t result = read_row(name, position, text);
        if(result != USERFS_OK)
            return result;
    }
    if(page)
        page->count = row;
    return USERFS_OK;
}

static userfs_result_t show_page(micropython_view_state_t *ui, code_position_t start)
{
    code_position_t end = start;
    code_page_t page = {0};
    userfs_result_t result = read_page(ui->selected_name, &end, &page);
    if(result != USERFS_OK)
        return result;
    if(!page.count)
    {
        memcpy(page.text[0], "(empty)", sizeof("(empty)"));
        page.line_number[0] = 1U;
        page.count = 1U;
    }
    g_code.current = start;
    g_code.next = end;
    code_position_t *checkpoint = &g_code.checkpoints[start.offset / CHECKPOINT_BYTES];
    if(!checkpoint->line)
        *checkpoint = start;
    memcpy(ui->preview, page.text, sizeof(ui->preview));
    memcpy(ui->preview_line_number, page.line_number, sizeof(ui->preview_line_number));
    ui->preview_count = page.count;
    ui->preview_line = start.line;
    ui->preview_has_previous = start.offset != 0U;
    ui->preview_has_next = end.offset < g_code.size;
    return USERFS_OK;
}

userfs_result_t micropython_preview_open(micropython_view_state_t *ui, uint8_t columns)
{
    ui->preview_count = 0U;
    ui->preview_line = 1U;
    ui->preview_has_next = ui->preview_has_previous = 0U;
    memset(&g_code, 0, sizeof(g_code));
    if(!columns || columns > MICROPYTHON_PREVIEW_COLUMNS || columns % TAB_WIDTH)
        return USERFS_ERROR_INVALID_ARGUMENT;
    g_code.columns = columns;
    userfs_result_t result = userfs_stat(ui->selected_name, &g_code.size);
    if(result != USERFS_OK)
        return result;
    if(g_code.size > USERFS_FILE_MAX)
        return USERFS_ERROR_SIZE;
    return show_page(ui, (code_position_t){ .line = 1U });
}

userfs_result_t micropython_preview_move(micropython_view_state_t *ui, int8_t direction)
{
    if(direction > 0)
        return ui->preview_has_next ? show_page(ui, g_code.next) : USERFS_OK;
    if(!ui->preview_has_previous)
        return USERFS_OK;

    /* Reconstruct the prior page from the nearest earlier checkpoint. This
     * bounds backward reads without storing a position for every source row. */
    uint32_t bucket = g_code.current.offset / CHECKPOINT_BYTES;
    while(!g_code.checkpoints[bucket].line ||
          g_code.checkpoints[bucket].offset >= g_code.current.offset)
        --bucket;
    code_position_t position = g_code.checkpoints[bucket];
    code_position_t previous;
    do
    {
        previous = position;
        userfs_result_t result = read_page(ui->selected_name, &position, NULL);
        if(result != USERFS_OK)
            return result;
    }
    while(position.offset < g_code.current.offset);
    if(position.offset != g_code.current.offset)
        return USERFS_ERROR_IO;
    return show_page(ui, previous);
}
