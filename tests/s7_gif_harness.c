#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/apps/files/image_decode.h"
static const uint8_t gif[] = {
 'G','I','F','8','9','a',1,0,1,0,0x80,0,0, 0,0,0,255,255,255,
 0x21,0xf9,4,0,2,0,0,0, 0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,
 0x21,0xf9,4,0,3,0,0,0, 0x2c,0,0,0,0,1,0,1,0,0,2,2,0x4c,1,0,0x3b
};
static uint8_t row[16];
static unsigned frames, ended, pixels;
uint8_t *image_decode_row_buffer(void) { return row; }
uint32_t image_decode_row_buffer_size(void) { return sizeof(row); }
uint8_t fat_stream_open(fat_stream_t *stream,const fat_file_entry_t *entry,uint32_t offset)
{ (void)entry; memset(stream,0,sizeof(*stream)); stream->file_offset=offset; return 1; }
uint8_t fat_stream_read(fat_stream_t *stream,uint8_t *dst,uint32_t length)
{ if(stream->file_offset+length>sizeof(gif)) return 0; memcpy(dst,gif+stream->file_offset,length); stream->file_offset+=length; return 1; }
static uint8_t begin(void *ctx,uint16_t w,uint16_t h,uint16_t bg)
{ (void)ctx; (void)bg; assert(w==1 && h==1); return 1; }
static uint8_t frame_begin(void *ctx,const file_gif_frame_t *frame)
{ (void)ctx; assert(frame->delay_ms == (frames%2 ? 30 : 20)); return 1; }
static void draw(void *ctx,const file_gif_frame_t *frame,uint16_t y,const uint8_t *indices,const uint16_t *palette,uint16_t size)
{ (void)ctx; (void)frame; (void)palette; assert(y==0 && size==2 && indices[0]==frames%2); pixels++; }
static uint8_t end_frame(void *ctx) { (void)ctx; frames++; return 1; }
static void end(void *ctx) { (void)ctx; ended++; }
int main(void)
{
 fat_file_entry_t entry={0}; entry.size=sizeof(gif);
 file_image_sink_t sink={.animation_begin=begin,.animation_frame_begin=frame_begin,
 .animation_render_indexed_row=draw,.animation_frame_end=end_frame,.animation_end=end};
 assert(files_open_gif(&entry,&sink)==FILE_RESULT_OK && frames==1);
 assert(files_gif_tick(0)==FILE_RESULT_OK);
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
 puts("S7_GIF_OK timing=1 loop=1 pause=1 pixels=1");
}
