#include "qr_decoder_engine.h"

#include <string.h>

#include "quirc.h"

static struct quirc *g_qr_decoder;
static uint16_t g_qr_decoder_w;
static uint16_t g_qr_decoder_h;

static uint16_t qr_clip_coordinate(int coordinate, uint16_t extent)
{
    if(coordinate < 0)
        return 0U;
    return coordinate < extent ? (uint16_t)coordinate : (uint16_t)(extent - 1U);
}

void qr_decoder_engine_close(void)
{
    if(g_qr_decoder)
        quirc_destroy(g_qr_decoder);
    g_qr_decoder = NULL;
    g_qr_decoder_w = 0U;
    g_qr_decoder_h = 0U;
}

uint8_t *qr_decoder_engine_prepare(uint16_t width, uint16_t height)
{
    if(!g_qr_decoder)
        g_qr_decoder = quirc_new();
    if(!g_qr_decoder)
        return NULL;
    if(width != g_qr_decoder_w || height != g_qr_decoder_h)
    {
        if(quirc_resize(g_qr_decoder, width, height) < 0)
            return NULL;
        g_qr_decoder_w = width;
        g_qr_decoder_h = height;
    }
    return quirc_begin(g_qr_decoder, NULL, NULL);
}

void qr_decoder_engine_scan(uint8_t *image, qr_engine_scan_t *scan)
{
    static struct quirc_code code;
    static struct quirc_data data;

    scan->found = 0U;
    scan->payload_len = 0U;
    (void)quirc_begin(g_qr_decoder, NULL, NULL);
    quirc_end_with_image(g_qr_decoder, image);
    scan->code_count = quirc_count(g_qr_decoder);

    for(int index = 0; index < scan->code_count; index++)
    {
        quirc_decode_error_t error;

        quirc_extract(g_qr_decoder, index, &code);
        error = quirc_decode(&code, &data);
        if(error != QUIRC_SUCCESS)
        {
            quirc_flip(&code);
            error = quirc_decode(&code, &data);
        }
        if(error == QUIRC_SUCCESS)
        {
            scan->found = 1U;
            scan->x0 = g_qr_decoder_w - 1U;
            scan->y0 = g_qr_decoder_h - 1U;
            scan->x1 = 0U;
            scan->y1 = 0U;
            for(uint8_t i = 0U; i < 4U; i++)
            {
                uint16_t x = qr_clip_coordinate(code.corners[i].x, g_qr_decoder_w);
                uint16_t y = qr_clip_coordinate(code.corners[i].y, g_qr_decoder_h);
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
