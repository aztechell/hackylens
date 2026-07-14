#include "screenshot_bmp.h"

#include "../config/display_config.h"
#include "../config/photo_storage_config.h"
#include "photo_format.h"

uint32_t screenshot_bmp_file_size(void)
{
    return SCREENSHOT_BMP_HEADER_SIZE + photo_bmp24_row_bytes(LCD_W) * LCD_H;
}

void screenshot_bmp_fill_bytes(const screenshot_pixel_source_t *source, uint32_t offset, uint8_t *dst, uint16_t len)
{
    uint8_t header[SCREENSHOT_BMP_HEADER_SIZE];
    uint32_t row_bytes = photo_bmp24_row_bytes(LCD_W);
    uint32_t data_size = row_bytes * LCD_H;

    photo_make_bmp_header(header, LCD_W, LCD_H);

    for(uint16_t i = 0; i < len; i++)
    {
        uint32_t pos = offset + i;
        if(pos < SCREENSHOT_BMP_HEADER_SIZE)
        {
            dst[i] = header[pos];
            continue;
        }

        uint32_t data_pos = pos - SCREENSHOT_BMP_HEADER_SIZE;
        if(data_pos >= data_size)
        {
            dst[i] = 0;
            continue;
        }

        uint32_t row = data_pos / row_bytes;
        uint32_t col_byte = data_pos % row_bytes;
        uint32_t x = col_byte / 3U;
        uint32_t y = LCD_H - 1U - row;
        if(x >= LCD_W)
        {
            dst[i] = 0;
            continue;
        }

        uint16_t color = source && source->pixel_at ? source->pixel_at(source->context, (uint16_t)x, (uint16_t)y) : 0;
        uint8_t component = (uint8_t)(col_byte % 3U);
        if(component == 0)
            dst[i] = (uint8_t)((color & 0x001FU) * 255U / 31U);
        else if(component == 1)
            dst[i] = (uint8_t)(((color >> 5) & 0x003FU) * 255U / 63U);
        else
            dst[i] = (uint8_t)(((color >> 11) & 0x001FU) * 255U / 31U);
    }
}
