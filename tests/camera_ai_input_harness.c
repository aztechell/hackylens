#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "camera_ai_input.h"
#include "camera_stream.h"
#include "hal_dvp.h"

static uint16_t frame[320 * 240];
static unsigned ai_enabled;
static camera_stream_convert_start_hook_t start_hook;
static camera_stream_convert_finish_hook_t finish_hook;
static void *hook_context;
static int owner, other;

camera_input_format_t camera_sensor_input_format(void)
{ return CAMERA_PREVIEW_ROTATE ? CAMERA_INPUT_YUV422 : CAMERA_INPUT_RGB565; }
void *hk_shared_buffer_uncached(void *pointer) { return pointer; }
void hal_dvp_output_ai(uint8_t enabled) { ai_enabled = enabled; }
void hal_dvp_set_ai_rgb888(uint32_t r, uint32_t g, uint32_t b)
{ assert(g - r == 76800 && b - g == 76800); ai_enabled = 1; }
void camera_stream_set_convert_hooks(camera_stream_convert_start_hook_t start,
    camera_stream_convert_finish_hook_t finish, void *context)
{ start_hook = start; finish_hook = finish; hook_context = context; }
void camera_stream_clear_convert_hooks(void *context)
{ assert(context == &owner); start_hook = NULL; finish_hook = NULL; }

int main(void)
{
    uint32_t sequence = 99;
    assert(!camera_ai_input_acquire(NULL, 320, 240, 230400));
    assert(!camera_ai_input_acquire(&owner, 320, 240, 230399));
    uint8_t *input = camera_ai_input_acquire(&owner, 320, 240, 230400);
    assert(input && !camera_ai_input_acquire(&other, 320, 240, 230400));
    assert(camera_ai_input_attach(&owner));
    assert(!camera_ai_input_take(&owner, &sequence) && sequence == 99);
#if CAMERA_PREVIEW_ROTATE
    assert(!ai_enabled && !start_hook && !finish_hook);
    for(unsigned i = 0; i < 76800; i++) frame[i] = 0xF800;
    camera_ai_input_prepare_preview(frame, 160, 120, 5);
    assert(!camera_ai_input_take(&owner, &sequence));
    camera_ai_input_prepare_preview(frame, 320, 240, 5);
    assert(!camera_ai_input_take(&other, &sequence));
    assert(camera_ai_input_take(&owner, &sequence) && sequence == 5);
    assert(input[80] == 255 && input[76880] == 0 && input[153680] == 0);
    for(unsigned i = 0; i < 76800; i++) frame[i] = 0x001F;
    camera_ai_input_prepare_preview(frame, 320, 240, 6);
    assert(input[80] == 255 && !camera_ai_input_take(&owner, &sequence));
    camera_ai_input_arm(&owner);
    camera_ai_input_prepare_preview(frame, 320, 240, 6);
    assert(camera_ai_input_take(&owner, &sequence) && sequence == 6);
    assert(input[80] == 0 && input[153680] == 255 && !ai_enabled);
#else
    assert(start_hook && finish_hook && !ai_enabled);
    start_hook(hook_context);
    assert(ai_enabled);
    memset(input, 42, 230400);
    finish_hook(7, hook_context);
    assert(!ai_enabled && camera_ai_input_take(&owner, &sequence) && sequence == 7);
    camera_ai_input_prepare_preview(frame, 320, 240, 8);
    assert(input[80] == 42 && !camera_ai_input_take(&owner, &sequence));
#endif
    camera_ai_input_release(&other);
    assert(camera_ai_input_data(&owner) == input);
    camera_ai_input_release(&owner);
    assert(!camera_ai_input_data(&owner) && !ai_enabled);
    camera_ai_input_prepare_preview(frame, 320, 240, 9);
    assert(!camera_ai_input_take(&owner, &sequence));
    puts("CAMERA_AI_INPUT_OK");
    return 0;
}
