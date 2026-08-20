#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/display.h>

#include "capability_provider.h"
#include "display_provider.h"
#include "lcd_st7789_transport.h"

#define TEST_WIDTH 320U
#define TEST_HEIGHT 240U
#define TEST_FRAME_BYTES (TEST_WIDTH * TEST_HEIGHT * 2U)

extern const hk_capability_provider_t hk_k210_display_provider;

static uint8_t s_shadow[TEST_FRAME_BYTES] __attribute__((aligned(4)));
static uint64_t s_now_us;
static uint64_t s_transfer_bytes;
static uint32_t s_write_calls;
static uint32_t s_begin_calls;
static hk_display_rect_t s_last_window;

typedef struct
{
    uint32_t probes;
    uint32_t cancel_after;
} cancel_fixture_t;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void lcd_st7789_transport_prepare(void) {}
void lcd_st7789_transport_init(void) {}

hk_result_t lcd_st7789_transport_begin(
    const hk_display_rect_t *rect, hk_deadline_t deadline)
{
    if(deadline.at_us && s_now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(!rect || rect->x < 0 || rect->y < 0 ||
       (uint64_t)(uint32_t)rect->x + rect->width > TEST_WIDTH ||
       (uint64_t)(uint32_t)rect->y + rect->height > TEST_HEIGHT)
        return HK_ERR_INVALID_ARGUMENT;
    s_last_window = *rect;
    s_begin_calls++;
    return HK_OK;
}

hk_result_t lcd_st7789_transport_write(
    const uint8_t *pixels, size_t size_bytes,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(!pixels || size_bytes == 0U || size_bytes > 128U)
        return HK_ERR_INVALID_ARGUMENT;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us && s_now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    s_transfer_bytes += size_bytes;
    s_write_calls++;
    s_now_us += 10U;
    return HK_OK;
}

uint8_t *lcd_st7789_transport_shadow(void) { return s_shadow; }
uint32_t lcd_st7789_transport_shadow_size(void) { return sizeof(s_shadow); }
uint32_t lcd_st7789_transport_stride(void) { return TEST_WIDTH * 2U; }
uint16_t lcd_st7789_transport_shadow_pixel(uint16_t x, uint16_t y)
{
    uint32_t offset = ((uint32_t)y * TEST_WIDTH + x) * 2U;
    return (uint16_t)((uint16_t)s_shadow[offset] << 8) |
           s_shadow[offset + 1U];
}

static uint8_t cancel_probe(const void *context)
{
    cancel_fixture_t *fixture = (cancel_fixture_t *)context;
    return (uint8_t)(fixture->probes++ >= fixture->cancel_after);
}

static hk_lease_t lease(uint32_t slot, hk_owner_t owner)
{
    hk_lease_t result = {
        slot, 1U, owner, HK_CAPABILITY_ID_DISPLAY,
    };
    return result;
}

int main(void)
{
    hk_display_provider_t *provider =
        (hk_display_provider_t *)hk_k210_display_provider.context;
    hk_owner_t base_owner = {1U, 1U};
    hk_owner_t overlay_owner = {2U, 1U};
    hk_owner_t intruder = {3U, 1U};
    hk_lease_t base = lease(1U, base_owner);
    hk_lease_t overlay = lease(2U, overlay_owner);
    hk_lease_t other = lease(3U, intruder);
    hk_display_info_t info;
    hk_display_surface_t surface;
    hk_display_rect_t pixel_a = {0, 0, 1U, 1U};
    hk_display_rect_t pixel_b = {319, 239, 1U, 1U};
    hk_display_rect_t clipped = {-2, -1, 4U, 3U};
    hk_deadline_t deadline = {1000000U};
    uint64_t before;

    require_true(provider && provider->context,
                 "capability provider must expose private adapter state");
    require_true(provider->open_plane(provider->context, &base,
                                      HK_DISPLAY_PLANE_BASE) == HK_OK &&
                 provider->open_plane(provider->context, &overlay,
                                      HK_DISPLAY_PLANE_OVERLAY) == HK_OK &&
                 provider->open_plane(provider->context, &other,
                                      HK_DISPLAY_PLANE_BASE) == HK_ERR_BUSY,
                 "BASE and OVERLAY must be independently exclusive");

    require_true(provider->get_info(provider->context, &info) == HK_OK &&
                 info.width == TEST_WIDTH && info.height == TEST_HEIGHT &&
                 info.maximum_commands == 32U &&
                 info.transfer_slice_bytes == 128U &&
                 info.maximum_present_duration_us == 500000U,
                 "adapter info must publish bounded physical limits");

    require_true(provider->begin_batch(provider->context, &overlay) == HK_OK &&
                 provider->clear(provider->context, &overlay, 0x0000U) == HK_OK &&
                 provider->surface_acquire(provider->context, &base,
                                           &surface) == HK_OK,
                 "borrowed BASE must coexist with a staged OVERLAY batch");
    ((uint8_t *)surface.pixels.data)[0] = 0x12U;
    ((uint8_t *)surface.pixels.data)[1] = 0x34U;
    require_true(provider->mark_dirty(provider->context, &base, &pixel_a) == HK_OK,
                 "surface damage must be explicit");
    before = s_transfer_bytes;
    require_true(provider->present(provider->context, &base,
                                   deadline, NULL) == HK_OK &&
                 s_transfer_bytes - before == 2U,
                 "native BASE surface must present only its dirty pixel");

    {
        cancel_fixture_t fixture = {0U, 8U};
        hk_cancel_t cancel = {cancel_probe, &fixture};
        uint16_t commands;
        uint16_t text_bytes;

        require_true(provider->present(provider->context, &overlay,
                                       deadline, &cancel) == HK_ERR_CANCELLED &&
                     provider->stage_checkpoint(
                         provider->context, &overlay,
                         &commands, &text_bytes) == HK_OK &&
                     commands == 1U && text_bytes == 0U,
                     "cancel must preserve the unchanged overlay stage");
        before = s_transfer_bytes;
        require_true(provider->present(provider->context, &overlay,
                                       deadline, NULL) == HK_OK &&
                     s_transfer_bytes - before <= TEST_FRAME_BYTES * 2U,
                     "retry must repair then commit without extra framebuffer traffic");
    }

    require_true(provider->surface_acquire(provider->context, &base,
                                           &surface) == HK_OK &&
                 provider->mark_dirty(provider->context, &base, &pixel_a) == HK_OK &&
                 provider->mark_dirty(provider->context, &base, &pixel_b) == HK_OK,
                 "disjoint BASE damage must fit the bounded dirty list");
    before = s_transfer_bytes;
    require_true(provider->present(provider->context, &base,
                                   deadline, NULL) == HK_OK &&
                 s_transfer_bytes - before == 4U &&
                 s_last_window.width == 1U && s_last_window.height == 1U,
                 "far-apart dirty pixels must not promote to fullscreen");

    require_true(provider->begin_batch(provider->context, &base) == HK_OK &&
                 provider->fill_rect(provider->context, &base,
                                     &clipped, 0x07E0U) == HK_OK,
                 "BASE primitive must clip transactionally");
    before = s_transfer_bytes;
    require_true(provider->present(provider->context, &base,
                                   deadline, NULL) == HK_OK &&
                 s_transfer_bytes - before == 8U,
                 "clipped 2x2 primitive must transfer exactly eight bytes");

    require_true(provider->begin_batch(provider->context, &base) == HK_OK,
                 "bounded command fixture must begin");
    for(uint16_t index = 0U; index < info.maximum_commands; index++)
        require_true(provider->fill_rect(provider->context, &base,
                                         &pixel_a, index) == HK_OK,
                     "adapter must accept its advertised command capacity");
    require_true(provider->fill_rect(provider->context, &base,
                                     &pixel_a, 0U) == HK_ERR_LIMIT &&
                 provider->abort(provider->context, &base) == HK_OK,
                 "command overflow must be explicit and abortable");

    before = s_transfer_bytes;
    require_true(provider->close_plane(provider->context, &overlay,
                                       deadline) == HK_OK &&
                 s_transfer_bytes - before == TEST_FRAME_BYTES,
                 "overlay release must restore the current base exactly once");
    require_true(provider->close_plane(provider->context, &base,
                                       deadline) == HK_OK,
                 "clean BASE release must be bounded and idempotent at core level");
    require_true(s_begin_calls > 0U && s_write_calls > 0U,
                 "adapter suite must exercise the raw panel transport");

    printf("K210_DISPLAY_ADAPTER_OK cases=8 slice=128 framebuffer=153600\n");
    return 0;
}
