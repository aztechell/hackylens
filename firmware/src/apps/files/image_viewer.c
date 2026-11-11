#include "image_decode.h"

#include <string.h>

#include "image_viewer.h"
#include "image_decode_gif.h"
#include "files_firmware.h"

typedef file_result_t (*image_decoder_t)(const fat_file_entry_t *, const file_image_sink_t *);

static image_decoder_t files_image_decoder(const fat_file_entry_t *entry)
{
    static const struct
    {
        char extension[5];
        image_decoder_t decode;
    } formats[] = {
        {".bmp", files_open_bmp},
        {".raw", files_open_raw565},
        {".ppm", files_open_ppm},
        {".png", files_open_png},
        {".gif", files_open_gif},
    };

    if(!entry || (entry->attr & FAT_ATTR_DIR))
        return NULL;
    const char *extension = strrchr(entry->name, '.');
    if(!extension)
        return NULL;
    for(size_t i = 0U; i < sizeof(formats) / sizeof(formats[0]); i++)
        if(str_eq_ci(extension, formats[i].extension))
            return formats[i].decode;
    return NULL;
}

uint8_t files_entry_is_image(const fat_file_entry_t *entry)
{
    return files_image_decoder(entry) != NULL;
}

file_result_t files_open_image_entry(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    files_gif_close();
    image_decoder_t decode = files_image_decoder(entry);
    return decode ? decode(entry, sink) : FILE_RESULT_UNSUPPORTED_FORMAT;
}

file_result_t files_image_viewer_tick(uint64_t now_us)
{
    return files_gif_tick(now_us);
}

void files_image_viewer_close(void)
{
    files_gif_close();
}

uint8_t files_image_viewer_is_animation(void)
{
    return files_gif_is_animation();
}

uint8_t files_image_viewer_toggle_pause(uint64_t now_us)
{
    return files_gif_toggle_pause(now_us);
}
