#ifndef HK_INPUT_PROVIDER_H
#define HK_INPUT_PROVIDER_H
#include <hackylens/capability/input.h>
struct hk_input
{
    void *context;
    hk_result_t (*open_cursor)(void *, hk_input_cursor_t *);
    hk_result_t (*get_info)(void *, hk_input_info_t *);
    hk_result_t (*get_state)(void *, uint32_t *);
    hk_result_t (*next_event)(void *, hk_input_cursor_t *, hk_input_event_t *);
};
extern const hk_input_t hk_input_binding;
#endif
