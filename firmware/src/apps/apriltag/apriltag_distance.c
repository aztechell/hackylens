#include "apriltag_distance.h"

#include <math.h>

#include "../../services/apriltag_decoder.h"
#include "apriltag_config.h"

#define CENTER_X ((APRILTAG_INPUT_W - 1U) * 0.5f)
#define CENTER_Y ((APRILTAG_INPUT_H - 1U) * 0.5f)
#define MIN_EDGE_PX 12.0f

typedef struct
{
    apriltag_vector_t u;
    apriltag_vector_t v;
    apriltag_vector_t center;
} tag_projection_t;

static float dot(apriltag_vector_t a, apriltag_vector_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float apriltag_distance_norm(apriltag_vector_t v)
{
    return sqrtf(dot(v, v));
}

static apriltag_vector_t scale(apriltag_vector_t v, float factor)
{
    return (apriltag_vector_t){v.x * factor, v.y * factor, v.z * factor};
}

static apriltag_vector_t subtract(apriltag_vector_t a, apriltag_vector_t b)
{
    return (apriltag_vector_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static apriltag_vector_t cross(apriltag_vector_t a, apriltag_vector_t b)
{
    return (apriltag_vector_t){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                                a.x * b.y - a.y * b.x};
}

/* Project a unit square ordered (0,0), (1,0), (1,1), (0,1). The detector
 * orders corners cyclically. The starting corner does not affect the range. */
static uint8_t projection(const float corners[4][2], float min_edge_px, tag_projection_t *p)
{
    float winding = 0.0f;
    for(unsigned i = 0; i < 4U; i++)
    {
        const float *a = corners[i];
        const float *b = corners[(i + 1U) % 4U];
        const float *c = corners[(i + 2U) % 4U];
        if(!(a[0] >= 1.0f && a[0] <= APRILTAG_INPUT_W - 2.0f &&
             a[1] >= 1.0f && a[1] <= APRILTAG_INPUT_H - 2.0f))
            return 0U;
        float dx = b[0] - a[0], dy = b[1] - a[1];
        float turn = dx * (c[1] - b[1]) - dy * (c[0] - b[0]);
        if(dx * dx + dy * dy < min_edge_px * min_edge_px ||
           fabsf(turn) < 1.0f || (i && winding * turn <= 0.0f))
            return 0U;
        winding = turn;
    }

    const float *a = corners[0], *b = corners[1], *c = corners[2], *d = corners[3];
    float bx = b[0] - c[0], dx = d[0] - c[0], sx = a[0] - b[0] + c[0] - d[0];
    float by = b[1] - c[1], dy = d[1] - c[1], sy = a[1] - b[1] + c[1] - d[1];
    float det = bx * dy - dx * by;
    if(fabsf(det) < 1.0f)
        return 0U;
    float g = (sx * dy - dx * sy) / det;
    float h = (bx * sy - sx * by) / det;
    if(1.0f + g <= 0.0f || 1.0f + h <= 0.0f || 1.0f + g + h <= 0.0f)
        return 0U;

    p->u = (apriltag_vector_t){b[0] - a[0] + (b[0] - CENTER_X) * g,
                               b[1] - a[1] + (b[1] - CENTER_Y) * g, g};
    p->v = (apriltag_vector_t){d[0] - a[0] + (d[0] - CENTER_X) * h,
                               d[1] - a[1] + (d[1] - CENTER_Y) * h, h};
    p->center = (apriltag_vector_t){a[0] - CENTER_X + 0.5f * (p->u.x + p->v.x),
                                    a[1] - CENTER_Y + 0.5f * (p->u.y + p->v.y),
                                    1.0f + 0.5f * (g + h)};
    return 1U;
}

uint8_t apriltag_distance_calibrate(const float corners[4][2], uint16_t tag_mm,
                                   uint16_t reference_mm, float *focal_px)
{
    tag_projection_t p;
    if(!tag_mm || !reference_mm || !projection(corners, MIN_EDGE_PX, &p) ||
       fabsf(p.u.z) > 0.06f || fabsf(p.v.z) > 0.06f)
        return 0U;
    float u = hypotf(p.u.x, p.u.y) / p.center.z;
    float v = hypotf(p.v.x, p.v.y) / p.center.z;
    if(fminf(u, v) < 20.0f || fabsf(u - v) > 0.08f * (u + v) ||
       fabsf(p.center.x / p.center.z) > 10.0f ||
       fabsf(p.center.y / p.center.z) > 10.0f)
        return 0U;
    float focal = (u + v) * 0.5f * reference_mm / tag_mm;
    if(focal < APRILTAG_DISTANCE_FOCAL_MIN_PX || focal > APRILTAG_DISTANCE_FOCAL_MAX_PX)
        return 0U;
    *focal_px = focal;
    return 1U;
}

/* A small distant tag is nearly affine. Its unforeshortened scale is the
 * largest singular value of the averaged edge vectors. This avoids using
 * subpixel differences between opposite edges to infer a steep tilt. */
static uint8_t estimate_small_tag(const float corners[4][2], uint16_t tag_mm,
                                  float focal_px, apriltag_vector_t *translation)
{
    const float *a = corners[0], *b = corners[1], *c = corners[2], *d = corners[3];
    apriltag_vector_t u = {(b[0] - a[0] + c[0] - d[0]) * 0.5f,
                           (b[1] - a[1] + c[1] - d[1]) * 0.5f, 0.0f};
    apriltag_vector_t v = {(d[0] - a[0] + c[0] - b[0]) * 0.5f,
                           (d[1] - a[1] + c[1] - b[1]) * 0.5f, 0.0f};
    const float uu = dot(u, u), vv = dot(v, v), uv = dot(u, v);
    const float spread = hypotf(uu - vv, 2.0f * uv);
    const float large = (uu + vv + spread) * 0.5f;
    const float small = (uu + vv - spread) * 0.5f;
    const float skew_x = a[0] + c[0] - b[0] - d[0];
    const float skew_y = a[1] + c[1] - b[1] - d[1];
    if(large > 16.0f * 16.0f || small < 0.36f * large ||
       skew_x * skew_x + skew_y * skew_y > 0.04f * large)
        return 0U;

    apriltag_vector_t ray = {
        ((a[0] + b[0] + c[0] + d[0]) * 0.25f - CENTER_X) / focal_px,
        ((a[1] + b[1] + c[1] + d[1]) * 0.25f - CENTER_Y) / focal_px,
        1.0f,
    };
    /* Away from the center, perspective contributes to the apparent scale
     * even for a distant tilted tag. Use the perspective fit there. */
    if(ray.x * ray.x + ray.y * ray.y > 0.0625f)
        return 0U;
    *translation = scale(ray, focal_px * tag_mm / sqrtf(large));
    return 1U;
}

apriltag_range_quality_t apriltag_distance_pose(const float corners[4][2], uint16_t tag_mm,
                                               float focal_px, uint8_t native_resolution,
                                               apriltag_vector_t *translation)
{
    tag_projection_t p;
    /* A native 320x240 window retains twice the samples per unit in the
     * full-frame 160x120 calibration coordinates. */
    const float min_edge_px = native_resolution ? MIN_EDGE_PX * 0.5f : MIN_EDGE_PX;
    if(!tag_mm || !(focal_px >= APRILTAG_DISTANCE_FOCAL_MIN_PX &&
                    focal_px <= APRILTAG_DISTANCE_FOCAL_MAX_PX) ||
       !projection(corners, min_edge_px, &p))
        return APRILTAG_RANGE_NONE;
    if(native_resolution && estimate_small_tag(corners, tag_mm, focal_px, translation))
        return APRILTAG_RANGE_APPROXIMATE;
    p.u.x /= focal_px;
    p.u.y /= focal_px;
    p.v.x /= focal_px;
    p.v.y /= focal_px;
    p.center.x /= focal_px;
    p.center.y /= focal_px;

    float u = apriltag_distance_norm(p.u);
    float v = apriltag_distance_norm(p.v);
    if(u <= 0.0f || v <= 0.0f ||
       fabsf(dot(p.u, p.v)) > 0.25f * u * v ||
       fabsf(u - v) > 0.14f * (u + v))
        return APRILTAG_RANGE_NONE;
    *translation = scale(p.center, 2.0f * tag_mm / (u + v));
    return APRILTAG_RANGE_POSE;
}

void apriltag_range_filter_add(apriltag_range_filter_t *filter, uint64_t now_us,
                              uint32_t mm, apriltag_range_quality_t quality)
{
    if(filter->count && now_us - filter->updated_us >= APRILTAG_RANGE_HOLD_US)
        filter->count = filter->next = filter->approximate = 0U;
    const uint8_t bit = (uint8_t)(1U << filter->next);
    filter->approximate &= (uint8_t)~bit;
    if(quality == APRILTAG_RANGE_APPROXIMATE)
        filter->approximate |= bit;
    filter->samples[filter->next] = mm;
    filter->next = (filter->next + 1U) % 3U;
    if(filter->count < 3U)
        filter->count++;
    filter->updated_us = now_us;
}

uint8_t apriltag_range_filter_read(const apriltag_range_filter_t *filter, uint64_t now_us,
                                   uint32_t *mm)
{
    if(!filter->count || now_us - filter->updated_us >= APRILTAG_RANGE_HOLD_US)
        return 0U;
    const uint32_t a = filter->samples[0];
    const uint32_t b = filter->samples[1];
    if(filter->count == 1U)
        *mm = a;
    else if(filter->count == 2U)
        *mm = (a + b) / 2U;
    else
    {
        const uint32_t c = filter->samples[2];
        const uint32_t low = a < b ? a : b;
        const uint32_t high = a > b ? a : b;
        *mm = c < low ? low : c > high ? high : c;
    }
    return 1U;
}

static uint8_t gravity(const int16_t sample[3], apriltag_vector_t *unit)
{
    apriltag_vector_t v = {sample[0], sample[1], sample[2]};
    float magnitude = apriltag_distance_norm(v);
    if(magnitude < 850.0f || magnitude > 1150.0f)
        return 0U;
    *unit = scale(v, 1.0f / magnitude);
    return 1U;
}

uint8_t apriltag_distance_horizontal(apriltag_vector_t translation,
                                     const int16_t upright[3], const int16_t lens_down[3],
                                     const int16_t current[3], float *horizontal_mm)
{
    apriltag_vector_t y, z, down;
    if(!gravity(upright, &y) || !gravity(lens_down, &z) || !gravity(current, &down))
        return 0U;
    float alignment = dot(y, z);
    if(fabsf(alignment) > 0.18f)
        return 0U;
    z = subtract(z, scale(y, alignment));
    z = scale(z, 1.0f / apriltag_distance_norm(z));
    apriltag_vector_t x = cross(y, z);
    apriltag_vector_t camera_down = {dot(down, x), dot(down, y), dot(down, z)};
    *horizontal_mm = apriltag_distance_norm(subtract(
        translation, scale(camera_down, dot(translation, camera_down))));
    return 1U;
}
