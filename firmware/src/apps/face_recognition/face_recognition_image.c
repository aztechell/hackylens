#include "face_recognition_image.h"

#include <math.h>

#define FRAME_W FACE_DETECT_INPUT_WIDTH
#define FRAME_H FACE_DETECT_INPUT_HEIGHT
#define FRAME_PIXELS (FRAME_W * FRAME_H)

static void resample(const uint8_t *frame, uint8_t *output, uint16_t side,
                     float a, float b, float c, float d, float tx, float ty)
{
    uint32_t plane = (uint32_t)side * side;
    for(uint16_t y = 0U; y < side; y++)
    {
        float sx = tx + b * y;
        float sy = ty + d * y;
        for(uint16_t x = 0U; x < side; x++, sx += a, sy += c)
        {
            int32_t ix = (int32_t)(sx + 0.5f);
            int32_t iy = (int32_t)(sy + 0.5f);
            uint32_t to = (uint32_t)y * side + x;
            if(sx < -0.5f || sy < -0.5f ||
               ix >= (int32_t)FRAME_W || iy >= (int32_t)FRAME_H)
            {
                output[to] = output[to + plane] = output[to + 2U * plane] = 0U;
                continue;
            }
            uint32_t from = (uint32_t)iy * FRAME_W + (uint32_t)ix;
            output[to] = frame[from];
            output[to + plane] = frame[from + FRAME_PIXELS];
            output[to + 2U * plane] = frame[from + 2U * FRAME_PIXELS];
        }
    }
}

void face_recognition_crop(const uint8_t *frame, const face_detect_box_t *box, uint8_t *output)
{
    float sx = box->w / (float)FACE_LANDMARK_SIDE;
    float sy = box->h / (float)FACE_LANDMARK_SIDE;
    resample(frame, output, FACE_LANDMARK_SIDE, sx, 0.0f, 0.0f, sy,
             box->x + (sx - 1.0f) * 0.5f, box->y + (sy - 1.0f) * 0.5f);
}

uint8_t face_recognition_align(const uint8_t *frame, const face_detect_box_t *box,
                              const float *landmarks, uint8_t *output)
{
    /* CanMV's 64px face template, in eye/eye/nose/mouth/mouth order. */
    static const float canonical[FACE_LANDMARK_COUNT][2] = {
        {21.88f, 29.54f}, {42.02f, 29.43f}, {32.01f, 41.00f},
        {23.74f, 52.78f}, {40.42f, 52.69f},
    };
    float points[FACE_LANDMARK_COUNT][2];
    float sx = 0.0f, sy = 0.0f, dx = 0.0f, dy = 0.0f;
    for(uint8_t i = 0U; i < FACE_LANDMARK_COUNT; i++)
    {
        float lx = landmarks[2U * i], ly = landmarks[2U * i + 1U];
        if(!isfinite(lx) || !isfinite(ly))
            return 0U;
        points[i][0] = box->x + box->w / (1.0f + expf(-lx));
        points[i][1] = box->y + box->h / (1.0f + expf(-ly));
        sx += points[i][0];
        sy += points[i][1];
        dx += canonical[i][0];
        dy += canonical[i][1];
    }
    if(points[1][0] - points[0][0] < box->w * 0.15f ||
       points[3][1] + points[4][1] <= points[0][1] + points[1][1])
        return 0U;
    sx /= FACE_LANDMARK_COUNT;
    sy /= FACE_LANDMARK_COUNT;
    dx /= FACE_LANDMARK_COUNT;
    dy /= FACE_LANDMARK_COUNT;

    float dot = 0.0f, cross = 0.0f, square = 0.0f;
    for(uint8_t i = 0U; i < FACE_LANDMARK_COUNT; i++)
    {
        float x = canonical[i][0] - dx, y = canonical[i][1] - dy;
        dot += x * (points[i][0] - sx) + y * (points[i][1] - sy);
        cross += x * (points[i][1] - sy) - y * (points[i][0] - sx);
        square += x * x + y * y;
    }
    /* Inverse similarity transform, from aligned output into the input frame. */
    float a = dot / square, b = cross / square;
    resample(frame, output, FACE_EMBEDDING_SIDE, a, -b, b, a,
             sx - a * dx + b * dy, sy - b * dx - a * dy);
    return 1U;
}

uint8_t face_recognition_encode(const float *embedding, int8_t *feature)
{
    float maximum = 0.0f;
    for(uint16_t i = 0U; i < FACE_GALLERY_DIM; i++)
    {
        if(!isfinite(embedding[i]))
            return 0U;
        if(fabsf(embedding[i]) > maximum)
            maximum = fabsf(embedding[i]);
    }
    if(maximum < 1.0e-8f)
        return 0U;
    for(uint16_t i = 0U; i < FACE_GALLERY_DIM; i++)
        feature[i] = (int8_t)roundf((embedding[i] / maximum) * 127.0f);
    return 1U;
}

float face_recognition_compare(const int8_t *left, const int8_t *right)
{
    int32_t dot = 0, ll = 0, rr = 0;
    for(uint16_t i = 0U; i < FACE_GALLERY_DIM; i++)
    {
        dot += left[i] * right[i];
        ll += left[i] * left[i];
        rr += right[i] * right[i];
    }
    return ll && rr ? dot / sqrtf((float)ll * rr) : -1.0f;
}
