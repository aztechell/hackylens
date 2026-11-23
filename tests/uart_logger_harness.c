#include "uart_logger_core.h"

#include <inttypes.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if(argc != 2)
        return 1;
    FILE *input = fopen(argv[1], "rb");
    if(!input)
        return 1;
    logger_stream_t stream;
    logger_stream_init(&stream);
    int byte;
    while((byte = fgetc(input)) != EOF)
    {
        logger_sample_t sample;
        if(logger_feed(&stream, (uint8_t)byte, &sample))
        {
            char row[16];
            if(!logger_csv_row(row, sizeof(row), sample))
                return 1;
            fputs(row, stdout);
        }
    }
    if(ferror(input))
        return 1;
    fclose(input);
    printf("S %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
           stream.accepted, stream.gaps, stream.crc_errors, stream.stale, stream.digest);
    for(uint8_t i = 0; i < logger_rect_count(&stream); ++i)
    {
        logger_rect_t rect = logger_rect(&stream, i);
        printf("R %u %u %u %u %u\n", rect.x, rect.y, rect.width, rect.height, rect.color);
    }
    return 0;
}
