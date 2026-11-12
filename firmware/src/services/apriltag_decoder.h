#ifndef HK_APRILTAG_DECODER_H
#define HK_APRILTAG_DECODER_H

#include <stdint.h>

#define APRILTAG_INPUT_W 160U
#define APRILTAG_INPUT_H 120U
#define APRILTAG_MAX_HAMMING 1
#define APRILTAG_REFINE_EDGES 0
#define APRILTAG_RESULT_MAX 8U

struct apriltag_detector;
struct apriltag_family;

typedef struct
{
    struct apriltag_detector *detector;
    struct apriltag_family *family;
} apriltag_decoder_t;

typedef struct
{
    uint16_t id;
    uint8_t hamming;
    uint16_t confidence;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t center_x;
    int16_t center_y;
    int16_t corners[4][2];
} apriltag_result_t;

/* One luma buffer is shared by both core-1 clients. Core 0 fills it only
 * while the client that owns the camera has no decode in progress. */
void apriltag_decoder_fill(const volatile uint16_t *pixels, uint16_t width, uint16_t height);
uint8_t apriltag_decoder_open(apriltag_decoder_t *decoder);
void apriltag_decoder_close(apriltag_decoder_t *decoder);
uint8_t apriltag_decoder_scan(apriltag_decoder_t *decoder, uint16_t width,
                              uint16_t height, uint8_t refine_edges,
                              apriltag_result_t items[APRILTAG_RESULT_MAX]);

#endif
