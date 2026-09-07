#include <hackylens/capability/input.h>

#include <stdio.h>
#include <string.h>

#include "input_normative_backend.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("INPUT_NORMATIVE_FAIL backend=%s line=%d\n",          \
                   input_normative_backend_name(), __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static int accepted(uint64_t start_us, uint32_t raw)
{
    CHECK(input_normative_backend_sample(start_us, raw) == HK_OK);
    CHECK(input_normative_backend_sample(start_us + 10000U, raw) == HK_OK);
    CHECK(input_normative_backend_sample(start_us + 20000U, raw) == HK_OK);
    return 0;
}

int main(void)
{
    const hk_input_t *input = hk_input_service();
    hk_input_cursor_t cursor_a = {0}, cursor_b = {0}, cursor_c = {0};
    hk_input_event_t event;
    hk_input_info_t info;
    uint32_t state;
    uint64_t now = 0U;

    input_normative_backend_reset();
    CHECK(input != NULL);
    CHECK(hk_input_cursor_open(input, &cursor_a) == HK_OK);
    CHECK(hk_input_cursor_open(input, &cursor_b) == HK_OK);
    CHECK(hk_input_get_info(input, &info) == HK_OK);
    CHECK(info.supported_buttons == HK_INPUT_BUTTON_ALL &&
          info.sample_interval_us == 10000U &&
          info.debounce_interval_us == 20000U &&
          info.event_capacity == 8U);

    /* Bounce does not become a stable edge. */
    now = 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, 0U) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_OK);
    CHECK(event.sequence == 1U && event.timestamp_us == now &&
          event.state == HK_INPUT_BUTTON_LEFT &&
          event.changed == HK_INPUT_BUTTON_LEFT &&
          event.pressed == HK_INPUT_BUTTON_LEFT && event.released == 0U);
    CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_PENDING);

    /* A held state never repeats, while the other cursor sees the same edge. */
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_PENDING);
    CHECK(hk_input_next_event(input, &cursor_b, &event) == HK_OK);
    CHECK(event.sequence == 1U && event.pressed == HK_INPUT_BUTTON_LEFT);

    now += 10000U;
    CHECK(accepted(now, 0U) == 0);
    now += 20000U;
    CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_OK);
    CHECK(event.released == HK_INPUT_BUTTON_LEFT && event.pressed == 0U);

    now += 10000U;
    CHECK(accepted(
        now, HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK) == 0);
    now += 20000U;
    CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_OK);
    CHECK(event.changed == (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK) &&
          event.pressed == event.changed);
    CHECK(hk_input_get_state(input, &state) == HK_OK);
    CHECK(state == (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK));

    /* Leave cursor B behind and overwrite its unread history. */
    for(uint32_t index = 0U; index < 9U; index++)
    {
        uint32_t raw = (index & 1U) ? HK_INPUT_BUTTON_BACK : 0U;
        now += 10000U;
        CHECK(accepted(now, raw) == 0);
        now += 20000U;
        CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_OK);
    }
    CHECK(hk_input_get_state(input, &state) == HK_OK);
    CHECK(hk_input_next_event(input, &cursor_b, &event) == HK_ERR_OVERFLOW);
    CHECK(event.dropped == 11U && event.state == state);
    CHECK(hk_input_next_event(input, &cursor_b, &event) == HK_PENDING);

    /* Cursor retirement is local; reopening skips old history without resetting state. */
    hk_input_cursor_close(&cursor_a);
    CHECK(hk_input_next_event(input, &cursor_a, &event) == HK_ERR_INVALID_STATE);
    CHECK(hk_input_get_state(input, &state) == HK_OK);
    CHECK(hk_input_cursor_open(input, &cursor_c) == HK_OK);
    CHECK(hk_input_next_event(input, &cursor_c, &event) == HK_PENDING);
    CHECK(hk_input_get_state(NULL, &state) == HK_ERR_CAPABILITY_ABSENT);
    CHECK(hk_input_get_state(input, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_input_cursor_open(input, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_input_next_event(input, NULL, &event) == HK_ERR_INVALID_ARGUMENT);
    hk_input_cursor_close(&cursor_c);
    hk_input_cursor_close(&cursor_b);

    printf("INPUT_NORMATIVE_OK backend=%s events=12 capacity=%u dropped=11\n",
           input_normative_backend_name(), (unsigned)HK_INPUT_EVENT_CAPACITY);
    return 0;
}
