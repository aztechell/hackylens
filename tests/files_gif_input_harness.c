#include <stdio.h>
#include "../firmware/src/capabilities/input_provider.h"
#include "../firmware/src/capabilities/input_state.h"
#include "../firmware/src/apps/files/files_presenter.c"

static hk_input_state_t input_state;
static uint64_t now_us;
static uint32_t raw_buttons;
static unsigned samples;

hk_result_t hk_input_get_state(const hk_input_t *input, uint32_t *state)
{
    (void)input;
    samples++;
    (void)hk_input_state_sample(&input_state, now_us, raw_buttons);
    return hk_input_state_get(&input_state, state);
}

#define CHECK(c) do { if(!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while(0)

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
    puts("GIF_INPUT_OK");
    return 0;
}
