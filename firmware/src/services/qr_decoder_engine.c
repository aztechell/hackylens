#include "qr_decoder_engine.h"

#include <string.h>

#include "quirc.h"

static uint16_t qr_clip_coordinate(int coordinate, uint16_t extent)
{
    if(coordinate < 0)
        return 0U;
    return coordinate < extent ? (uint16_t)coordinate : (uint16_t)(extent - 1U);
}

static uint8_t qr_decode_candidate(const struct quirc *decoder, int index,
                                   struct quirc_code *code, struct quirc_data *data)
{
    /* Retry sampling inside each module using the grid already found. */
    static const int8_t offsets[][2] = {{0, 0}, {0, 1}, {1, 0}, {-1, 0}, {0, -1}};
    for(size_t i = 0U; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        quirc_extract_offset(decoder, index, code, offsets[i][0], offsets[i][1]);
        quirc_decode_error_t error = quirc_decode(code, data);
        if(error == QUIRC_SUCCESS)
            return 1U;
        if(error == QUIRC_ERROR_INVALID_GRID_SIZE || error == QUIRC_ERROR_INVALID_VERSION)
            return 0U;
        quirc_flip(code);
        if(quirc_decode(code, data) == QUIRC_SUCCESS)
            return 1U;
    }
    return 0U;
}

void qr_decoder_engine_close(qr_decoder_engine_t *engine)
{
    if(engine->decoder)
        quirc_destroy(engine->decoder);
    *engine = (qr_decoder_engine_t){0};
}

uint8_t *qr_decoder_engine_prepare(qr_decoder_engine_t *engine,
                                   uint16_t width, uint16_t height)
{
    if(!engine->decoder)
        engine->decoder = quirc_new();
    if(!engine->decoder)
        return NULL;
    if(width != engine->width || height != engine->height)
    {
        if(quirc_resize(engine->decoder, width, height) < 0)
            return NULL;
        engine->width = width;
        engine->height = height;
    }
    return quirc_begin(engine->decoder, NULL, NULL);
}

void qr_decoder_engine_scan(qr_decoder_engine_t *engine,
                            uint8_t *image, qr_engine_scan_t *scan)
{
    static struct quirc_code code;
    static struct quirc_data data;

    scan->found = 0U;
    scan->payload_len = 0U;
    (void)quirc_begin(engine->decoder, NULL, NULL);
    quirc_end_with_image(engine->decoder, image);
    scan->code_count = quirc_count(engine->decoder);

    for(int index = 0; index < scan->code_count; index++)
    {
        if(qr_decode_candidate(engine->decoder, index, &code, &data))
        {
            scan->found = 1U;
            scan->x0 = engine->width - 1U;
            scan->y0 = engine->height - 1U;
            scan->x1 = 0U;
            scan->y1 = 0U;
            for(uint8_t i = 0U; i < 4U; i++)
            {
                uint16_t x = qr_clip_coordinate(code.corners[i].x, engine->width);
                uint16_t y = qr_clip_coordinate(code.corners[i].y, engine->height);
                if(x < scan->x0) scan->x0 = x;
                if(y < scan->y0) scan->y0 = y;
                if(x > scan->x1) scan->x1 = x;
                if(y > scan->y1) scan->y1 = y;
            }
            scan->payload_len = data.payload_len < (int)sizeof(scan->payload)
                ? (uint16_t)data.payload_len : sizeof(scan->payload);
            memcpy(scan->payload, data.payload, scan->payload_len);
            return;
        }
    }
}
