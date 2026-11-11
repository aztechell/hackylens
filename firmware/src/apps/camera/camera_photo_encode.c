#include "camera_photo_encode.h"

#include <string.h>

uint32_t photo_encode_begin(photo_encoder_t *encoder, const photo_source_t *source)
{
    photo_format_t format = source->format;
    encoder->source = source;
    encoder->bottom_up = format == PHOTO_FORMAT_BMP24 || format == PHOTO_FORMAT_BMP565;
    encoder->bytes_per_pixel = format == PHOTO_FORMAT_RAW565 || format == PHOTO_FORMAT_BMP565 ? 2U : 3U;
    encoder->row_bytes = (uint32_t)source->width * encoder->bytes_per_pixel;
    encoder->header_size = 0U;

    if(encoder->bottom_up)
    {
        encoder->row_bytes = (encoder->row_bytes + 3U) & ~3UL;
        encoder->header_size = photo_make_bmp_header(
            encoder->header, source->width, source->height, encoder->bytes_per_pixel);
    }
    else if(format == PHOTO_FORMAT_PPM)
        encoder->header_size = photo_make_ppm_header(encoder->header, source->width, source->height);

    return encoder->header_size + encoder->row_bytes * source->height;
}

void photo_encode_fill_file_bytes(const photo_encoder_t *encoder, uint32_t offset,
                                  uint8_t *dst, uint16_t length)
{
    const photo_source_t *source = encoder->source;
    if(offset < encoder->header_size)
    {
        uint16_t take = encoder->header_size - (uint16_t)offset;
        if(take > length)
            take = length;
        memcpy(dst, encoder->header + offset, take);
        dst += take;
        length -= take;
        offset += take;
    }
    if(!length)
        return;

    offset -= encoder->header_size;
    uint32_t row = offset / encoder->row_bytes;
    uint32_t column = offset % encoder->row_bytes;
    uint8_t bytes = encoder->bytes_per_pixel;

    while(length && row < source->height)
    {
        uint32_t x = column / bytes;
        uint8_t component = (uint8_t)(column % bytes);
        uint8_t pixel[3] = {0U, 0U, 0U};
        uint16_t take;
        if(x < source->width)
        {
            uint32_t y = encoder->bottom_up ? source->height - 1U - row : row;
            uint16_t color = source->read_pixel(source->ctx, x, y);
            if(bytes == 2U)
            {
                pixel[0] = (uint8_t)color;
                pixel[1] = (uint8_t)(color >> 8);
            }
            else
            {
                uint8_t red = (uint8_t)(((color >> 11) & 31U) * 255U / 31U);
                uint8_t blue = (uint8_t)((color & 31U) * 255U / 31U);
                pixel[0] = encoder->bottom_up ? blue : red;
                pixel[1] = (uint8_t)(((color >> 5) & 63U) * 255U / 63U);
                pixel[2] = encoder->bottom_up ? red : blue;
            }
        }

        take = bytes - component;
        if(take > encoder->row_bytes - column)
            take = (uint16_t)(encoder->row_bytes - column);
        if(take > length)
            take = length;
        memcpy(dst, pixel + component, take);
        dst += take;
        length -= take;
        column += take;
        if(column == encoder->row_bytes)
        {
            column = 0U;
            row++;
        }
    }
    if(length)
        memset(dst, 0, length);
}
