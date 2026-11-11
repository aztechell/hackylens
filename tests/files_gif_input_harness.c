#include <stdio.h>
#include <string.h>
#include "../firmware/src/capabilities/input_provider.h"
#include "../firmware/src/capabilities/input_state.h"
#include "../firmware/src/apps/files/files_presenter.c"

static hk_input_state_t input_state;
static uint64_t now_us;
static uint32_t raw_buttons;
static unsigned samples;
static unsigned image_begins, image_presents, image_cancels;
static unsigned loading_shown;
static unsigned unsupported_shown;
static file_result_t image_result;

hk_result_t hk_input_get_state(const hk_input_t *input, uint32_t *state)
{
    (void)input;
    samples++;
    (void)hk_input_state_sample(&input_state, now_us, raw_buttons);
    return hk_input_state_get(&input_state, state);
}

#define CHECK(c) do { if(!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while(0)

static uint8_t begin_image(void)
{
    image_begins++;
    return 1U;
}

static uint8_t end_image(uint8_t present)
{
    if(present)
        image_presents++;
    else
        image_cancels++;
    return 1U;
}

file_result_t files_open_image_entry(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    if(entry && loading_shown != image_begins + 1U)
        return FILE_RESULT_NOT_FOUND;
    if(!sink->begin(sink->context))
        return FILE_RESULT_READ_ERROR;
    return image_result;
}

static void draw_file_status(const char *name, const char *status)
{
    if(strcmp(name, "IMG00001.RAW") == 0 && strcmp(status, "LOADING...") == 0)
        loading_shown++;
    if(strcmp(name, "IMG00001.RAW") == 0 && strcmp(status, "UNSUPPORTED") == 0)
        unsupported_shown++;
}

int main(void)
{
    hk_input_t input = {0};
    hk_input_event_t event = {0};
    hk_input_cursor_t cursor = {0};
    hk_input_state_reset(&input_state);
    (void)hk_input_state_sample(&input_state, 0U, 0U);
    CHECK(hk_input_state_open_cursor(&input_state, &cursor) == HK_OK);
    files_presenter_bind_input(&input);

    /* One slow frame. A full press/release occurs before main dispatch resumes. */
    for(unsigned row = 1U; row <= 40U; row++) {
        now_us = row * 5000U;
        raw_buttons = row >= 4U && row < 16U ? HK_INPUT_BUTTON_BACK : 0U;
        files_presenter_animation_render_indexed_row(NULL, NULL, 0U, NULL, NULL, 0U);
    }
    CHECK(samples == 40U);
    CHECK(hk_input_state_next_event(&input_state, &cursor, &event) == HK_OK);
    CHECK(event.pressed == HK_INPUT_BUTTON_BACK);
    CHECK(hk_input_state_next_event(&input_state, &cursor, &event) == HK_OK);
    CHECK(event.changed == HK_INPUT_BUTTON_BACK && event.state == 0U);
    CHECK(hk_input_state_next_event(&input_state, &cursor, &event) == HK_PENDING);
    files_presenter_bind_input(NULL);
    files_presenter_animation_render_indexed_row(NULL, NULL, 0U, NULL, NULL, 0U);
    CHECK(samples == 40U);
    const fat_file_entry_t image = {.name = "IMG00001.RAW"};
    const files_view_ops_t image_view = {.begin_image = begin_image, .end_image = end_image,
                                        .draw_file_status = draw_file_status};
    files_view_register(&image_view);
    CHECK(files_presenter_render_image(&image) == FILE_RESULT_OK);
    CHECK(image_begins == 1U && image_presents == 1U && !image_cancels);
    image_result = FILE_RESULT_UNSUPPORTED_FORMAT;
    CHECK(files_presenter_render_image(&image) == FILE_RESULT_UNSUPPORTED_FORMAT);
    CHECK(image_begins == 2U && image_presents == 1U && image_cancels == 1U);
    CHECK(unsupported_shown == 1U);
    puts("GIF_INPUT_OK");
    return 0;
}
