#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/time.h>
#include "apriltag.h"
#include "common/pnm.h"
#include "tag36h11.h"
#include "../firmware/src/adapters/micropython/micropython_camera_codes.h"
#include "../firmware/src/services/camera_client.h"
#include "../firmware/src/services/qr_luma.h"
#include "qr_fixture.h"

static const char owner;
static uint16_t pixels[320U * 240U];
static uint8_t rotate;
static uint32_t copy_count;
static uint32_t local_pass_count;

/* The firmware build discards the bundled library's unused PNM file reader. */
pnm_t *pnm_create_from_file(const char *path) { (void)path; abort(); }
void pnm_destroy(pnm_t *image) { (void)image; abort(); }

void *hk_shared_buffer_uncached(void *data) { return data; }
const hk_time_t *hk_time_service(void) { return NULL; }
hk_result_t hk_time_now_us(const hk_time_t *service, uint64_t *now)
{
    (void)service;
    *now = 0U;
    return HK_OK;
}

hk_result_t camera_client_get(const void *client, uint32_t lease,
                              camera_preview_frame_t *frame)
{
    assert(client == &owner);
    if(lease != 7U)
        return HK_ERR_STALE_HANDLE;
    *frame = (camera_preview_frame_t){pixels, 320U, 240U, rotate, lease, 13U};
    copy_count++;
    return HK_OK;
}

micropython_binding_result_t micropython_binding_call(
    micropython_binding_op_t operation, const uint32_t arguments[6],
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_capacity, size_t *output_length)
{
    micropython_code_frame_t frame;
    uint8_t *qr_image;
    assert(operation == MICROPYTHON_BINDING_OP_CAMERA_CODE_FRAME);
    assert(input && input_length == sizeof(qr_image) && output_capacity == sizeof(frame));
    memcpy(&qr_image, input, sizeof(qr_image));
    assert((qr_image != NULL) == (arguments[1] == VISION_SOURCE_QR));
    if(arguments[2] == QR_PASS_LOCAL)
        local_pass_count++;
    *output_length = 0U;
    hk_result_t result = micropython_camera_codes_stage(
        &owner, arguments[0], (vision_source_t)arguments[1], arguments[2], qr_image, &frame);
    if(result == HK_ERR_STALE_HANDLE)
        return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
    assert(result == HK_OK);
    memcpy(output, &frame, sizeof(frame));
    *output_length = sizeof(frame);
    return MICROPYTHON_BINDING_OK;
}

static void draw_tag(uint32_t id)
{
    apriltag_family_t *family = tag36h11_create();
    image_u8_t *image = apriltag_to_image(family, id);
    memset(pixels, 0xff, sizeof(pixels));
    for(int y = 0; y < image->height * 12; y++)
        for(int x = 0; x < image->width * 12; x++)
            if(!image->buf[(y / 12) * image->stride + x / 12])
                pixels[(y + 48) * 320 + x + 88] = 0U;
    image_u8_destroy(image);
    tag36h11_destroy(family);
}

static void draw_uneven_qr(void)
{
    draw_qr_fixture(pixels, 320U, 240U, 0U);
    for(unsigned y = 0U; y < 240U; y++)
    {
        for(unsigned x = 0U; x < 320U; x++)
        {
            unsigned gray = x * 180U / 320U;
            if(pixels[y * 320U + x])
                gray += 68U;
            pixels[y * 320U + x] =
                (uint16_t)(((gray >> 3U) << 11U) |
                           ((gray >> 2U) << 5U) | (gray >> 3U));
        }
    }
}

/* A 3 px/module code sheared in both axes. Center-only extraction finds
 * the grid but fails error correction on this raster. */
static void draw_sheared_qr(void)
{
    for(int y = 0; y < 240; y++)
    {
        for(int x = 0; x < 320; x++)
        {
            int u = 16 * (x - 88) - 3 * (y - 48);
            int v = 16 * (y - 48) - 3 * (x - 88);
            unsigned black = u >= 0 && v >= 0 && u < 21 * 48 && v < 21 * 48 &&
                (qr_fixture_rows[v / 48] & (1U << (u / 48)));
            pixels[y * 320 + x] = black ? 0U : UINT16_MAX;
        }
    }
}

static void assert_rotation(vision_result_item_t original, vision_result_item_t rotated)
{
    assert(rotated.x0 == 240U - original.y1 && rotated.y0 == original.x0);
    assert(rotated.x1 == 240U - original.y0 && rotated.y1 == original.x1);
}

int main(void)
{
    vision_result_snapshot_t result;
    assert(micropython_camera_codes_scan(6U, VISION_SOURCE_QR, &result) ==
           MICROPYTHON_BINDING_ERROR_NOT_ACTIVE);
    assert(copy_count == 0U);
    draw_qr_fixture(pixels, 320U, 240U, 0U);
    pack_qr_fixture_dvp(pixels, 320U * 240U);
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_QR, &result) ==
           MICROPYTHON_BINDING_OK);
    assert(result.source == VISION_SOURCE_QR && result.frame_id == 13U);
    assert(result.count == 1U && result.width == 320U && result.height == 240U);
    assert(result.qr_length == 2U && memcmp(result.qr_payload, "HL", 2U) == 0);
    vision_result_item_t original = result.items[0];

    rotate = 1U;
    draw_qr_fixture(pixels, 320U, 240U, 1U);
    pack_qr_fixture_dvp(pixels, 320U * 240U);
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_QR, &result) ==
           MICROPYTHON_BINDING_OK);
    assert(copy_count == 3U && result.width == 240U && result.height == 320U);
    assert(result.count == 1U && result.qr_length == 2U);
    assert(memcmp(result.qr_payload, "HL", 2U) == 0);
    assert_rotation(original, result.items[0]);

    draw_uneven_qr();
    pack_qr_fixture_dvp(pixels, 320U * 240U);
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_QR, &result) ==
           MICROPYTHON_BINDING_OK);
    assert(local_pass_count == 1U);
    assert(result.count == 1U && result.qr_length == 2U);
    assert(memcmp(result.qr_payload, "HL", 2U) == 0);

    draw_sheared_qr();
    pack_qr_fixture_dvp(pixels, 320U * 240U);
    unsigned before = copy_count;
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_QR, &result) ==
           MICROPYTHON_BINDING_OK);
    assert(copy_count == before + 1U);
    assert(result.count == 1U && result.qr_length == 2U);
    assert(memcmp(result.qr_payload, "HL", 2U) == 0);

    draw_tag(42U);
    rotate = 0U;
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_APRILTAG, &result) ==
           MICROPYTHON_BINDING_OK);
    assert(result.count == 1U && result.items[0].id == 42U && result.items[0].flags == 0U);
    assert(result.frame_id == 13U && result.source == VISION_SOURCE_APRILTAG && !result.qr_length);
    original = result.items[0];
    rotate = 1U;
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_APRILTAG, &result) ==
           MICROPYTHON_BINDING_OK);
    assert_rotation(original, result.items[0]);
    micropython_camera_codes_cleanup();
    memset(pixels, 0xff, sizeof(pixels));
    assert(micropython_camera_codes_scan(7U, VISION_SOURCE_APRILTAG, &result) ==
           MICROPYTHON_BINDING_OK);
    assert(!result.count);
    micropython_camera_codes_cleanup();
    puts("MICROPYTHON_CODES_OK");
    return 0;
}
