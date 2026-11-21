#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/apps/files/image_decode.h"
static const uint8_t gif[] = {
 'G','I','F','8','9','a',1,0,1,0,0x80,0,0, 0,0,0,255,255,255,
 0x21,0xf9,4,0,2,0,0,0, 0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,
 0x21,0xf9,4,0,3,0,0,0, 0x2c,0,0,0,0,1,0,1,0,0,2,2,0x4c,1,0,0x3b
};
static uint8_t row[256];
static uint8_t large_gif[25000];
static const uint8_t *source = gif;
static size_t source_size = sizeof(gif);
static unsigned large_rows[128];
static unsigned frames, ended, pixels;
uint8_t *image_decode_row_buffer(void) { return row; }
uint32_t image_decode_row_buffer_size(void) { return sizeof(row); }
uint8_t fat_stream_open(fat_stream_t *stream,const fat_file_entry_t *entry,uint32_t offset)
{ (void)entry; memset(stream,0,sizeof(*stream)); stream->file_offset=offset; return 1; }
uint8_t fat_stream_read(fat_stream_t *stream,uint8_t *dst,uint32_t length)
{ if(stream->file_offset+length>source_size) return 0; memcpy(dst,source+stream->file_offset,length); stream->file_offset+=length; return 1; }
static uint8_t begin(void *ctx,uint16_t w,uint16_t h,uint16_t bg)
{ (void)ctx; (void)bg; assert(source == gif ? w==1 && h==1 : w==256 && h==128); return 1; }
static uint8_t frame_begin(void *ctx,const file_gif_frame_t *frame)
{ (void)ctx; assert(frame->delay_ms == (frames%2 ? 30 : 20)); return 1; }
static void draw(void *ctx,const file_gif_frame_t *frame,uint16_t y,const uint8_t *indices,const uint16_t *palette,uint16_t size)
{
    (void)ctx;
    (void)palette;
    assert(size == 2);
    if(source == gif)
        assert(y == 0 && indices[0] == frames % 2U);
    else
    {
        assert(y < 128U);
        large_rows[y]++;
        for(uint16_t x = 0; x < frame->frame_w; x++)
            assert(indices[x] == 0U);
    }
    pixels++;
}
static uint8_t end_frame(void *ctx) { (void)ctx; frames++; return 1; }
static void end(void *ctx) { (void)ctx; ended++; }

static void make_large_gif(void)
{
    static const uint8_t header[] = {
        'G','I','F','8','9','a',0,1,128,0,0x80,0,0, 0,0,0,255,255,255,
        0x2c,0,0,0,0,0,1,128,0,0x40,2,
    };
    size_t out = sizeof(header);
    memcpy(large_gif, header, out);
    /* Each 24-bit pattern is four CLEAR,PIXEL pairs at a fixed width of 3. */
    for(unsigned remaining = 256U * 128U / 4U; remaining > 0U;)
    {
        unsigned runs = remaining < 85U ? remaining : 85U;
        large_gif[out++] = (uint8_t)(runs * 3U);
        for(unsigned i = 0U; i < runs; i++)
        {
            large_gif[out++] = 0x04U;
            large_gif[out++] = 0x41U;
            large_gif[out++] = 0x10U;
        }
        remaining -= runs;
    }
    large_gif[out++] = 1U;
    large_gif[out++] = 5U;
    large_gif[out++] = 0U;
    large_gif[out++] = 0x3bU;
    source = large_gif;
    source_size = out;
}

static void test_large_frame(const file_image_sink_t *sink)
{
    make_large_gif();
    fat_file_entry_t entry = {.size = (uint32_t)source_size};
    frames = pixels = ended = 0U;

    assert(files_open_gif(&entry, sink) == FILE_RESULT_OK && frames == 0U && pixels == 0U);
    assert(files_gif_tick(0U) == FILE_RESULT_OK && frames == 0U && pixels > 0U);
    unsigned rows_before_pause = pixels;
    assert(files_gif_toggle_pause(5000U));
    assert(files_gif_tick(25000U) == FILE_RESULT_OK && pixels == rows_before_pause);
    assert(files_gif_toggle_pause(30000U));
    for(uint64_t now = 30000U; now < 500000U && frames == 0U; now += 5000U)
        assert(files_gif_tick(now) == FILE_RESULT_OK);
    assert(frames == 1U && pixels == 128U);
    for(unsigned row_index = 0U; row_index < 128U; row_index++)
        assert(large_rows[row_index] == 1U);
    files_gif_close();

    frames = pixels = 0U;
    assert(files_open_gif(&entry, sink) == FILE_RESULT_OK);
    assert(files_gif_tick(0U) == FILE_RESULT_OK && frames == 0U && pixels > 0U);
    files_gif_close();
    assert(!files_gif_active() && ended == 2U);
    assert(files_gif_tick(5000U) == FILE_RESULT_OK && frames == 0U);
}

int main(void)
{
 fat_file_entry_t entry={0}; entry.size=sizeof(gif);
 file_image_sink_t sink={.animation_begin=begin,.animation_frame_begin=frame_begin,
 .animation_render_indexed_row=draw,.animation_frame_end=end_frame,.animation_end=end};
 assert(files_open_gif(&entry,&sink)==FILE_RESULT_OK && frames==0);
 assert(files_gif_tick(0)==FILE_RESULT_OK && frames==1);
 assert(files_gif_tick(19999)==FILE_RESULT_OK && frames==1);
 assert(files_gif_tick(20000)==FILE_RESULT_OK && frames==2);
 /* A later poll must not restart the delay after decoding a frame. */
 assert(files_gif_tick(40000)==FILE_RESULT_OK && frames==2);
 assert(files_gif_tick(50000)==FILE_RESULT_OK && frames==3);
 assert(files_gif_toggle_pause(60000));
 assert(files_gif_tick(90000)==FILE_RESULT_OK && frames==3);
 assert(files_gif_toggle_pause(100000));
 assert(files_gif_tick(109999)==FILE_RESULT_OK && frames==3);
 assert(files_gif_tick(110000)==FILE_RESULT_OK && frames==4);
 assert(pixels==frames);
 files_gif_close(); assert(ended==1 && !files_gif_active());
 test_large_frame(&sink);
 puts("S7_GIF_OK timing=1 loop=1 pause=1 pixels=1 slices=1 cancel=1");
}
