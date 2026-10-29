#include <assert.h>
#include <stdio.h>
#include "../firmware/src/capabilities/input_provider.h"

static uint64_t now_us;
static uint32_t physical;
static uint8_t power_short;
uint8_t axp173_poll_back_press(uint64_t now)
{ (void)now; uint8_t result = power_short; power_short = 0U; return result; }
static hk_input_cursor_t cursor;
static unsigned ok_presses, ok_releases, back_presses, back_releases;
uint64_t hal_time_us(void) { return now_us; }
uint32_t buttons_read_pressed_mask(void) { return physical; }

static uint32_t advance(uint32_t raw, unsigned milliseconds)
{
    uint32_t state = 0;
    physical = raw;
    for(unsigned elapsed = 0; elapsed < milliseconds; elapsed += 10)
    {
        hk_input_event_t event;
        now_us += 10000;
        assert(hk_input_binding.get_state(hk_input_binding.context, &state) == HK_OK);
        while(hk_input_binding.next_event(hk_input_binding.context, &cursor, &event) == HK_OK)
        {
            assert(event.dropped == 0);
            ok_presses += !!(event.pressed & HK_INPUT_BUTTON_OK);
            ok_releases += !!(event.released & HK_INPUT_BUTTON_OK);
            back_presses += !!(event.pressed & HK_INPUT_BUTTON_BACK);
            back_releases += !!(event.released & HK_INPUT_BUTTON_BACK);
        }
    }
    return state;
}

int main(void)
{
    hk_input_info_t info;
    assert(hk_input_binding.get_info(hk_input_binding.context, &info) == HK_OK);
    assert(info.pulse_buttons == HK_INPUT_BUTTON_BACK);
    assert(hk_input_binding.open_cursor(hk_input_binding.context, &cursor) == HK_OK);
    /* Center has normal OK level/hold semantics, with portable debounce. */
    assert(advance(HK_INPUT_BUTTON_OK, 40) == HK_INPUT_BUTTON_OK);
    assert(ok_presses == 1 && ok_releases == 0 && back_presses == 0);
    assert(advance(HK_INPUT_BUTTON_OK, 800) == HK_INPUT_BUTTON_OK);
    assert(ok_presses == 1 && back_presses == 0);
    advance(0, 40);
    assert(ok_releases == 1);
    assert(advance(HK_INPUT_BUTTON_LEFT, 40) == HK_INPUT_BUTTON_LEFT);
    advance(0, 40);
    advance(HK_INPUT_BUTTON_OK, 10);
    advance(0, 100);
    assert(ok_presses == 1 && ok_releases == 1); /* Contact bounce. */
    power_short = 1U;
    advance(0, 100);
    assert(back_presses == 1 && back_releases == 1);
    assert(ok_presses == 1 && ok_releases == 1);
    advance(0, 400);
    assert(back_presses == 1);
    power_short = 1U;
    advance(0, 100);
    assert(back_presses == 2 && back_releases == 2);
    puts("CUBE_INPUT_OK center=normal-ok power-short=back bounce=ignored");
    return 0;
}
