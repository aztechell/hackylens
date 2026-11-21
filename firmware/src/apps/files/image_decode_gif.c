#include "image_decode_gif.h"

#include <string.h>

#include "files_image_config.h"

#define GIF_SIGNATURE_SIZE 6U
#define GIF_HEADER_SIZE 13U
#define GIF_PALETTE_MAX 256U
#define GIF_LZW_TABLE_SIZE 4096U
#define GIF_MIN_DELAY_MS 20U
#define GIF_SLICE_PIXELS 32768U
#define GIF_SLICE_BYTES 4096U

typedef struct
{
    uint8_t data[255];
    uint8_t size;
    uint8_t position;
    uint8_t finished;
    file_result_t failure;
} gif_subblocks_t;

typedef struct
{
    gif_subblocks_t blocks;
    uint32_t bits;
    uint8_t bit_count;
} gif_bits_t;

typedef enum
{
    GIF_READ_MARKER,
    GIF_SKIP_EXTENSION,
    GIF_READ_PIXELS,
    GIF_FINISH_IMAGE,
} gif_phase_t;

typedef struct
{
    gif_bits_t bits;
    uint32_t pixel_count;
    int32_t previous;
    uint16_t row_used;
    uint16_t clear_code;
    uint16_t end_code;
    uint16_t next_code;
    uint8_t code_width;
    uint8_t initial_width;
    uint8_t first;
} gif_lzw_t;

typedef struct
{
    fat_file_entry_t entry;
    fat_stream_t stream;
    file_image_sink_t sink;
    uint16_t global_palette[GIF_PALETTE_MAX];
    uint16_t local_palette[GIF_PALETTE_MAX];
    file_gif_frame_t frame;
    gif_lzw_t lzw;
    const uint16_t *frame_palette;
    uint16_t frame_palette_size;
    gif_phase_t phase;
    uint16_t canvas_w;
    uint16_t canvas_h;
    uint16_t global_palette_size;
    uint16_t background_rgb565;
    uint32_t delay_ms;
    uint64_t deadline_us;
    uint64_t pause_remaining_us;
    uint8_t frames_in_cycle;
    int16_t transparent_index;
    uint8_t disposal;
    uint8_t active;
    uint8_t playing;
    uint8_t paused;
    uint8_t deadline_armed;
    uint8_t known_animated;
    uint8_t interlaced;
} gif_session_t;

static gif_session_t g_gif;
static uint16_t g_lzw_prefix[GIF_LZW_TABLE_SIZE];
static uint8_t g_lzw_suffix[GIF_LZW_TABLE_SIZE];
static uint8_t g_lzw_stack[GIF_LZW_TABLE_SIZE];

static uint16_t gif_rd16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t gif_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

static file_result_t gif_read(void *dst, uint32_t len)
{
    if(g_gif.stream.file_offset > g_gif.entry.size ||
       len > g_gif.entry.size - g_gif.stream.file_offset)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(!fat_stream_read(&g_gif.stream, (uint8_t *)dst, len))
        return FILE_RESULT_READ_ERROR;
    return FILE_RESULT_OK;
}

static file_result_t gif_read_palette(uint16_t *palette, uint16_t count)
{
    uint8_t rgb[3];

    if(count == 0 || count > GIF_PALETTE_MAX)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    for(uint16_t i = 0; i < count; i++)
    {
        file_result_t result = gif_read(rgb, sizeof(rgb));
        if(result != FILE_RESULT_OK)
            return result;
        palette[i] = gif_rgb565(rgb[0], rgb[1], rgb[2]);
    }
    return FILE_RESULT_OK;
}

static int16_t gif_subblock_byte(gif_subblocks_t *blocks)
{
    file_result_t result;

    if(blocks->finished || blocks->failure != FILE_RESULT_OK)
        return -1;
    if(blocks->position >= blocks->size)
    {
        result = gif_read(&blocks->size, 1);
        if(result != FILE_RESULT_OK)
        {
            blocks->failure = result;
            return -1;
        }
        if(blocks->size == 0U)
        {
            blocks->finished = 1;
            return -1;
        }
        result = gif_read(blocks->data, blocks->size);
        if(result != FILE_RESULT_OK)
        {
            blocks->failure = result;
            return -1;
        }
        blocks->position = 0U;
    }
    return blocks->data[blocks->position++];
}

/* The current compressed block is already in memory. Skipping starts at
   the next sub-block length byte in the stream. */
static file_result_t gif_skip_block(uint8_t *finished)
{
    uint8_t size;
    file_result_t result = gif_read(&size, 1);
    if(result != FILE_RESULT_OK)
        return result;
    *finished = size == 0U;
    return size ? gif_read(g_gif.lzw.bits.blocks.data, size) : FILE_RESULT_OK;
}

static int32_t gif_read_code(gif_bits_t *bits, uint8_t width)
{
    while(bits->bit_count < width)
    {
        int16_t value = gif_subblock_byte(&bits->blocks);
        if(value < 0)
            return -1;
        bits->bits |= (uint32_t)(uint8_t)value << bits->bit_count;
        bits->bit_count = (uint8_t)(bits->bit_count + 8U);
    }
    {
        uint16_t code = (uint16_t)(bits->bits & ((1UL << width) - 1UL));
        bits->bits >>= width;
        bits->bit_count = (uint8_t)(bits->bit_count - width);
        return code;
    }
}

static uint16_t gif_interlace_row(uint16_t sequence, uint16_t height)
{
    static const uint8_t starts[4] = {0, 4, 2, 1};
    static const uint8_t steps[4] = {8, 8, 4, 2};

    for(uint8_t pass = 0; pass < 4; pass++)
    {
        if(starts[pass] >= height)
            continue;
        uint16_t rows = (uint16_t)(1U + (height - 1U - starts[pass]) / steps[pass]);
        if(sequence < rows)
            return (uint16_t)(starts[pass] + sequence * steps[pass]);
        sequence -= rows;
    }
    return height;
}

static file_result_t gif_emit_index(uint8_t index)
{
    gif_lzw_t *lzw = &g_gif.lzw;
    const file_gif_frame_t *frame = &g_gif.frame;
    uint8_t *row = image_decode_row_buffer();

    if(index >= g_gif.frame_palette_size ||
       lzw->pixel_count >= (uint32_t)frame->frame_w * frame->frame_h)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    row[lzw->row_used++] = index;
    lzw->pixel_count++;
    if(lzw->row_used == frame->frame_w)
    {
        uint16_t row_index = (uint16_t)((lzw->pixel_count / frame->frame_w) - 1U);
        if(g_gif.interlaced)
            row_index = gif_interlace_row(row_index, frame->frame_h);
        g_gif.sink.animation_render_indexed_row(g_gif.sink.context, frame, row_index,
                                                row, g_gif.frame_palette, g_gif.frame_palette_size);
        lzw->row_used = 0U;
    }
    return FILE_RESULT_OK;
}

static void gif_lzw_reset_codes(gif_lzw_t *lzw)
{
    lzw->next_code = (uint16_t)(lzw->end_code + 1U);
    lzw->previous = -1;
}

static file_result_t gif_begin_lzw(void)
{
    gif_lzw_t *lzw = &g_gif.lzw;
    uint8_t minimum_code_size;
    file_result_t result = gif_read(&minimum_code_size, 1);
    if(result != FILE_RESULT_OK)
        return result;
    if(minimum_code_size < 2U || minimum_code_size > 8U ||
       g_gif.frame.frame_w > image_decode_row_buffer_size())
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    memset(lzw, 0, sizeof(*lzw));
    lzw->clear_code = (uint16_t)(1U << minimum_code_size);
    lzw->end_code = (uint16_t)(lzw->clear_code + 1U);
    lzw->code_width = (uint8_t)(minimum_code_size + 1U);
    lzw->initial_width = lzw->code_width;
    gif_lzw_reset_codes(lzw);
    return FILE_RESULT_OK;
}

/* Finish one dictionary code before yielding. A code expands to at most
   the size of the fixed LZW table, even at the end of a slice. */
static file_result_t gif_decode_code(void)
{
    gif_lzw_t *lzw = &g_gif.lzw;
    int32_t raw_code = gif_read_code(&lzw->bits, lzw->code_width);
    uint16_t stack_size = 0U;

    if(raw_code < 0)
    {
        return lzw->bits.blocks.failure != FILE_RESULT_OK ?
               lzw->bits.blocks.failure : FILE_RESULT_UNSUPPORTED_FORMAT;
    }
    uint16_t code = (uint16_t)raw_code;
    if(code == lzw->clear_code)
    {
        lzw->code_width = lzw->initial_width;
        gif_lzw_reset_codes(lzw);
        return FILE_RESULT_OK;
    }
    if(code == lzw->end_code)
    {
        if(lzw->pixel_count != (uint32_t)g_gif.frame.frame_w * g_gif.frame.frame_h)
            return FILE_RESULT_UNSUPPORTED_FORMAT;
        g_gif.phase = GIF_FINISH_IMAGE;
        return FILE_RESULT_OK;
    }
    if(code > lzw->next_code || (code == lzw->next_code && lzw->previous < 0))
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    if(code == lzw->next_code)
    {
        g_lzw_stack[stack_size++] = lzw->first;
        code = (uint16_t)lzw->previous;
    }
    while(code >= lzw->clear_code)
    {
        if(code >= lzw->next_code || stack_size >= GIF_LZW_TABLE_SIZE)
            return FILE_RESULT_UNSUPPORTED_FORMAT;
        g_lzw_stack[stack_size++] = g_lzw_suffix[code];
        code = g_lzw_prefix[code];
    }
    lzw->first = (uint8_t)code;
    if(stack_size >= GIF_LZW_TABLE_SIZE)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    g_lzw_stack[stack_size++] = lzw->first;
    while(stack_size)
    {
        file_result_t result = gif_emit_index(g_lzw_stack[--stack_size]);
        if(result != FILE_RESULT_OK)
            return result;
    }

    if(lzw->previous >= 0 && lzw->next_code < GIF_LZW_TABLE_SIZE)
    {
        g_lzw_prefix[lzw->next_code] = (uint16_t)lzw->previous;
        g_lzw_suffix[lzw->next_code] = lzw->first;
        lzw->next_code++;
        if(lzw->next_code == (1U << lzw->code_width) && lzw->code_width < 12U)
            lzw->code_width++;
    }
    lzw->previous = raw_code;
    return FILE_RESULT_OK;
}

static void gif_reset_graphics_control(void)
{
    g_gif.delay_ms = GIF_MIN_DELAY_MS;
    g_gif.transparent_index = -1;
    g_gif.disposal = 0;
}

static file_result_t gif_read_graphics_control(void)
{
    uint8_t data[6];
    uint16_t delay;
    file_result_t result;

    result = gif_read(data, sizeof(data));
    if(result != FILE_RESULT_OK)
        return result;
    if(data[0] != 4 || data[5] != 0)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    g_gif.disposal = (uint8_t)((data[1] >> 2) & 0x07U);
    if(g_gif.disposal > 3)
        g_gif.disposal = 0;
    delay = gif_rd16(&data[2]);
    g_gif.delay_ms = delay < 2U ? GIF_MIN_DELAY_MS : (uint32_t)delay * 10U;
    g_gif.transparent_index = (data[1] & 0x01U) ? data[4] : -1;
    return FILE_RESULT_OK;
}

static file_result_t gif_read_application_extension(void)
{
    uint8_t size;
    uint8_t app[255];
    file_result_t result;

    result = gif_read(&size, 1);
    if(result != FILE_RESULT_OK)
        return result;
    if(size == 0)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    result = gif_read(app, size);
    if(result != FILE_RESULT_OK)
        return result;
    return FILE_RESULT_OK;
}

static file_result_t gif_skip_extension_header(void)
{
    uint8_t size;
    uint8_t scratch[255];
    file_result_t result;

    result = gif_read(&size, 1);
    if(result != FILE_RESULT_OK)
        return result;
    if(size)
    {
        result = gif_read(scratch, size);
        if(result != FILE_RESULT_OK)
            return result;
    }
    return FILE_RESULT_OK;
}

static file_result_t gif_begin_frame(void)
{
    uint8_t descriptor[9];
    uint8_t packed;
    uint8_t interlaced;
    uint16_t palette_size;
    uint16_t *palette;
    file_gif_frame_t frame;
    file_result_t result;

    result = gif_read(descriptor, sizeof(descriptor));
    if(result != FILE_RESULT_OK)
        return result;
    memset(&frame, 0, sizeof(frame));
    frame.canvas_w = g_gif.canvas_w;
    frame.canvas_h = g_gif.canvas_h;
    frame.frame_x = gif_rd16(&descriptor[0]);
    frame.frame_y = gif_rd16(&descriptor[2]);
    frame.frame_w = gif_rd16(&descriptor[4]);
    frame.frame_h = gif_rd16(&descriptor[6]);
    frame.background_rgb565 = g_gif.background_rgb565;
    frame.delay_ms = g_gif.delay_ms;
    frame.transparent_index = g_gif.transparent_index;
    frame.disposal = g_gif.disposal;
    packed = descriptor[8];
    interlaced = (packed & 0x40U) ? 1U : 0U;

    if(frame.frame_w == 0 || frame.frame_h == 0 ||
       (uint32_t)frame.frame_x + frame.frame_w > frame.canvas_w ||
       (uint32_t)frame.frame_y + frame.frame_h > frame.canvas_h)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(frame.frame_w > PHOTO_VIEW_MAX_W || frame.frame_h > PHOTO_VIEW_MAX_H)
        return FILE_RESULT_TOO_LARGE;

    if(packed & 0x80U)
    {
        palette_size = (uint16_t)(2U << (packed & 0x07U));
        result = gif_read_palette(g_gif.local_palette, palette_size);
        if(result != FILE_RESULT_OK)
            return result;
        palette = g_gif.local_palette;
    }
    else
    {
        if(g_gif.global_palette_size == 0)
            return FILE_RESULT_UNSUPPORTED_FORMAT;
        palette_size = g_gif.global_palette_size;
        palette = g_gif.global_palette;
    }
    if(frame.transparent_index >= (int16_t)palette_size)
        frame.transparent_index = -1;

    g_gif.frame = frame;
    g_gif.frame_palette = palette;
    g_gif.frame_palette_size = palette_size;
    g_gif.interlaced = interlaced;
    result = gif_begin_lzw();
    if(result != FILE_RESULT_OK)
        return result;
    if(!g_gif.sink.animation_frame_begin(g_gif.sink.context, &g_gif.frame))
        return FILE_RESULT_READ_ERROR;
    g_gif.phase = GIF_READ_PIXELS;
    return FILE_RESULT_OK;
}

static file_result_t gif_finish_frame(uint64_t now_us)
{
    if(!g_gif.sink.animation_frame_end(g_gif.sink.context))
        return FILE_RESULT_READ_ERROR;

    if(g_gif.frames_in_cycle < 2U)
        g_gif.frames_in_cycle++;
    if(g_gif.frames_in_cycle == 2U)
        g_gif.known_animated = 1;
    g_gif.deadline_us = now_us + (uint64_t)g_gif.frame.delay_ms * 1000ULL;
    g_gif.deadline_armed = 1U;
    g_gif.phase = GIF_READ_MARKER;
    gif_reset_graphics_control();
    return FILE_RESULT_OK;
}

static file_result_t gif_parse_header(uint8_t reset_canvas)
{
    uint8_t header[GIF_HEADER_SIZE];
    uint8_t packed;
    uint8_t background_index;
    file_result_t result;

    if(!fat_stream_open(&g_gif.stream, &g_gif.entry, 0))
        return FILE_RESULT_READ_ERROR;
    result = gif_read(header, sizeof(header));
    if(result != FILE_RESULT_OK)
        return result;
    if((memcmp(header, "GIF87a", GIF_SIGNATURE_SIZE) != 0 &&
        memcmp(header, "GIF89a", GIF_SIGNATURE_SIZE) != 0))
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    g_gif.canvas_w = gif_rd16(&header[6]);
    g_gif.canvas_h = gif_rd16(&header[8]);
    if(g_gif.canvas_w == 0 || g_gif.canvas_h == 0 ||
       g_gif.canvas_w > PHOTO_VIEW_MAX_W || g_gif.canvas_h > PHOTO_VIEW_MAX_H)
        return FILE_RESULT_TOO_LARGE;

    packed = header[10];
    background_index = header[11];
    g_gif.global_palette_size = 0;
    g_gif.background_rgb565 = 0;
    if(packed & 0x80U)
    {
        g_gif.global_palette_size = (uint16_t)(2U << (packed & 0x07U));
        result = gif_read_palette(g_gif.global_palette, g_gif.global_palette_size);
        if(result != FILE_RESULT_OK)
            return result;
        if(background_index < g_gif.global_palette_size)
            g_gif.background_rgb565 = g_gif.global_palette[background_index];
    }
    gif_reset_graphics_control();
    g_gif.frames_in_cycle = 0;
    if(reset_canvas && !g_gif.sink.animation_begin(g_gif.sink.context, g_gif.canvas_w,
                                                    g_gif.canvas_h, g_gif.background_rgb565))
        return FILE_RESULT_READ_ERROR;
    return FILE_RESULT_OK;
}

static file_result_t gif_read_marker(void)
{
    uint8_t marker;
    file_result_t result = gif_read(&marker, 1);
    if(result != FILE_RESULT_OK)
        return result;
    if(marker == 0x2CU)
        return gif_begin_frame();
    if(marker == 0x3BU)
    {
        if(g_gif.frames_in_cycle == 0U)
            return FILE_RESULT_UNSUPPORTED_FORMAT;
        if(!g_gif.known_animated)
        {
            g_gif.playing = 0U;
            return FILE_RESULT_OK;
        }
        return gif_parse_header(1U);
    }
    if(marker == 0x21U)
    {
        uint8_t label;
        result = gif_read(&label, 1);
        if(result != FILE_RESULT_OK)
            return result;
        if(label == 0xF9U)
            return gif_read_graphics_control();
        if(label == 0xFFU)
            result = gif_read_application_extension();
        else if(label == 0x01U)
            result = gif_skip_extension_header();
        g_gif.phase = GIF_SKIP_EXTENSION;
        return result;
    }
    return FILE_RESULT_UNSUPPORTED_FORMAT;
}

static file_result_t gif_decode_slice(uint64_t now_us)
{
    uint32_t pixels = 0U;
    uint32_t bytes = 0U;

    while(g_gif.playing && pixels < GIF_SLICE_PIXELS && bytes < GIF_SLICE_BYTES)
    {
        uint32_t offset = g_gif.stream.file_offset;
        file_result_t result;

        if(g_gif.phase == GIF_READ_MARKER)
            result = gif_read_marker();
        else if(g_gif.phase == GIF_READ_PIXELS)
        {
            uint32_t before = g_gif.lzw.pixel_count;
            result = gif_decode_code();
            pixels += g_gif.lzw.pixel_count - before;
        }
        else
        {
            uint8_t finished;
            result = gif_skip_block(&finished);
            if(result == FILE_RESULT_OK && finished)
            {
                if(g_gif.phase == GIF_FINISH_IMAGE)
                    return gif_finish_frame(now_us);
                g_gif.phase = GIF_READ_MARKER;
            }
        }

        if(result != FILE_RESULT_OK)
            return result;
        /* At the loop trailer the stream reopens at the header. */
        bytes += g_gif.stream.file_offset >= offset ?
                 g_gif.stream.file_offset - offset : g_gif.stream.file_offset + 1U;
    }
    return FILE_RESULT_OK;
}

void files_gif_close(void)
{
    if(g_gif.active && g_gif.sink.animation_end)
        g_gif.sink.animation_end(g_gif.sink.context);
    memset(&g_gif, 0, sizeof(g_gif));
}

file_result_t files_open_gif(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    file_result_t result;

    files_gif_close();
    if(!entry || !sink || !sink->animation_begin || !sink->animation_frame_begin ||
       !sink->animation_render_indexed_row || !sink->animation_frame_end)
        return FILE_RESULT_NOT_FOUND;
    g_gif.entry = *entry;
    g_gif.sink = *sink;
    g_gif.active = 1;
    g_gif.playing = 1;
    result = gif_parse_header(1U);
    if(result != FILE_RESULT_OK)
        files_gif_close();
    return result;
}

file_result_t files_gif_tick(uint64_t now_us)
{
    file_result_t result;

    if(!g_gif.active || !g_gif.playing || g_gif.paused)
        return FILE_RESULT_OK;
    if(g_gif.deadline_armed && now_us < g_gif.deadline_us)
        return FILE_RESULT_OK;

    /* The FAT stream and periodic SD health checks share the FAT sector
       scratch buffer. Another tick may have replaced the cached sector
       while the stream was suspended. */
    g_gif.stream.cache_valid = 0;
    g_gif.deadline_armed = 0;
    result = gif_decode_slice(now_us);
    if(result != FILE_RESULT_OK)
        files_gif_close();
    return result;
}

uint8_t files_gif_active(void)
{
    return g_gif.active;
}

uint8_t files_gif_is_animation(void)
{
    return g_gif.active && (g_gif.playing || g_gif.known_animated);
}

uint8_t files_gif_toggle_pause(uint64_t now_us)
{
    if(!files_gif_is_animation() || !g_gif.playing)
        return 0;
    if(g_gif.paused)
    {
        g_gif.deadline_us = now_us + g_gif.pause_remaining_us;
        g_gif.deadline_armed = 1;
        g_gif.paused = 0;
    }
    else
    {
        g_gif.pause_remaining_us =
            g_gif.deadline_armed && g_gif.deadline_us > now_us ? g_gif.deadline_us - now_us : 0;
        g_gif.paused = 1;
    }
    return 1;
}
