#include "camera_photo_format.h"

#include <stdio.h>
#include <string.h>

#include "../core/hk_binary.h"

const char *photo_format_label(photo_format_t format)
{
    if(format == PHOTO_FORMAT_RAW565)
        return "RAW565";
    if(format == PHOTO_FORMAT_BMP565)
        return "BMP565";
    if(format == PHOTO_FORMAT_PPM)
        return "PPM";
    return "BMP24";
}

const char *photo_format_ext(photo_format_t format)
{
    if(format == PHOTO_FORMAT_RAW565)
        return "RAW";
    if(format == PHOTO_FORMAT_PPM)
        return "PPM";
    return "BMP";
}

uint16_t photo_make_ppm_header(uint8_t header[PHOTO_PPM_HEADER_MAX], uint16_t width, uint16_t height)
{
    int len = snprintf((char *)header, PHOTO_PPM_HEADER_MAX, "P6\n%u %u\n255\n", width, height);
    if(len < 0)
        return 0;
    if((uint32_t)len >= PHOTO_PPM_HEADER_MAX)
        return PHOTO_PPM_HEADER_MAX - 1U;
    return (uint16_t)len;
}

uint16_t photo_make_bmp_header(uint8_t *header, uint16_t width, uint16_t height,
                               uint8_t bytes_per_pixel)
{
    uint8_t masked = bytes_per_pixel == 2U;
    uint16_t header_size = masked ? PHOTO_BMP565_HEADER_SIZE : PHOTO_BMP_HEADER_SIZE;
    uint32_t row_bytes = (((uint32_t)width * bytes_per_pixel) + 3U) & ~3UL;
    uint32_t data_size = row_bytes * height;

    memset(header, 0, header_size);
    header[0] = 'B';
    header[1] = 'M';
    wr32(&header[2], header_size + data_size);
    wr32(&header[10], header_size);
    wr32(&header[14], 40U);
    wr32(&header[18], width);
    wr32(&header[22], height);
    wr16(&header[26], 1U);
    wr16(&header[28], (uint16_t)bytes_per_pixel * 8U);
    wr32(&header[34], data_size);
    if(masked)
    {
        wr32(&header[30], 3U);
        wr32(&header[54], 0x0000F800UL);
        wr32(&header[58], 0x000007E0UL);
        wr32(&header[62], 0x0000001FUL);
    }
    return header_size;
}
