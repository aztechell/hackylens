#include "camera_data_save.h"

#include <string.h>

#include "camera_client.h"
#include "camera_photo_encode.h"
#include "../config/sd_config.h"
#include "../core/camera_pixel_order.h"

static struct
{
    const void *owner;
    uint32_t lease;
    uint32_t offset;
    uint8_t writing;
} s_save;

typedef struct
{
    const photo_encoder_t *encoder;
    uint8_t *sector;
    uint32_t size;
} photo_run_t;

static uint16_t read_pixel(void *context, uint32_t x, uint32_t y)
{
    const camera_preview_frame_t *frame = context;
    const uint32_t src_x = frame->rotate ? y : x;
    const uint32_t src_y = frame->rotate ? frame->height - 1U - x : y;
    return frame->pixels[src_y * frame->width + (src_x ^ camera_dvp_pair_xor(frame->width))];
}

static const uint8_t *encode_sector(void *context, uint16_t index)
{
    const photo_run_t *run = context;
    uint32_t offset = s_save.offset + (uint32_t)index * SD_BLOCK_SIZE;
    uint32_t count = run->size - offset;
    if(count > SD_BLOCK_SIZE)
        count = SD_BLOCK_SIZE;
    photo_encode_fill_file_bytes(run->encoder, offset, run->sector, count);
    if(count < SD_BLOCK_SIZE)
        memset(run->sector + count, 0, SD_BLOCK_SIZE - count);
    return run->sector;
}

uint8_t camera_data_save_holds(const void *owner, uint32_t lease)
{
    return owner && s_save.owner == owner && s_save.lease == lease;
}

void camera_data_save_cancel(const void *owner)
{
    if(!owner || s_save.owner != owner)
        return;
    script_sd_abort(&s_save);
    s_save.owner = NULL;
}

script_sd_result_t camera_data_save_begin(const void *owner, uint32_t lease,
                                          const char *name, size_t length)
{
    if(s_save.owner && (!camera_data_save_holds(owner, lease) || s_save.writing))
        return SCRIPT_SD_BUSY;
    camera_preview_frame_t frame;
    if(camera_client_get(owner, lease, &frame) != HK_OK)
        return SCRIPT_SD_STALE_FRAME;
    script_sd_result_t result = script_sd_begin(&s_save, name, length, 1U);
    if(result == SCRIPT_SD_PENDING || result == SCRIPT_SD_OK)
    {
        s_save.owner = owner;
        s_save.lease = lease;
        s_save.offset = 0U;
        s_save.writing = result == SCRIPT_SD_OK;
    }
    else if(result != SCRIPT_SD_BUSY && result != SCRIPT_SD_INVALID_NAME)
        s_save.owner = NULL;
    return result;
}

script_sd_result_t camera_data_save_step(const void *owner)
{
    if(!owner || s_save.owner != owner || !s_save.writing)
        return SCRIPT_SD_BUSY;
    camera_preview_frame_t frame;
    if(camera_client_get(owner, s_save.lease, &frame) != HK_OK)
    {
        camera_data_save_cancel(owner);
        return SCRIPT_SD_STALE_FRAME;
    }
    const photo_source_t source = {
        .format = PHOTO_FORMAT_BMP24,
        .width = frame.rotate ? frame.height : frame.width,
        .height = frame.rotate ? frame.width : frame.height,
        .read_pixel = read_pixel,
        .ctx = &frame,
    };
    photo_encoder_t encoder;
    uint32_t total = photo_encode_begin(&encoder, &source);
    if(s_save.offset == total)
    {
        script_sd_result_t result = script_sd_finish(&s_save);
        if(result != SCRIPT_SD_PENDING)
            s_save.owner = NULL;
        return result;
    }
    uint8_t sector[SD_BLOCK_SIZE];
    photo_run_t run = {&encoder, sector, total};
    size_t written;
    script_sd_result_t result = script_sd_write_run(&s_save, total - s_save.offset,
                                                    encode_sector, &run, &written);
    if(result != SCRIPT_SD_OK && result != SCRIPT_SD_PENDING)
    {
        s_save.owner = NULL;
        return result;
    }
    s_save.offset += written;
    return SCRIPT_SD_PENDING;
}
