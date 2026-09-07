#include <hackylens/capability/input.h>
#include <stddef.h>
#include <string.h>
#include "input_provider.h"

const hk_input_t *hk_input_service(void)
{
    return hk_input_binding.get_state ? &hk_input_binding : NULL;
}

static hk_result_t input_validate(const hk_input_t *input)
{
    if(!input)
        return HK_ERR_CAPABILITY_ABSENT;
    if(!input->open_cursor || !input->get_info || !input->get_state || !input->next_event)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

hk_result_t hk_input_cursor_open(const hk_input_t *input, hk_input_cursor_t *cursor)
{
    hk_result_t result;
    if(!cursor)
        return HK_ERR_INVALID_ARGUMENT;
    memset(cursor, 0, sizeof(*cursor));
    result = input_validate(input);
    return result == HK_OK ? input->open_cursor(input->context, cursor) : result;
}

void hk_input_cursor_close(hk_input_cursor_t *cursor)
{
    if(cursor)
        memset(cursor, 0, sizeof(*cursor));
}

hk_result_t hk_input_get_info(const hk_input_t *input, hk_input_info_t *info)
{
    hk_result_t result;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = input_validate(input);
    return result == HK_OK ? input->get_info(input->context, info) : result;
}

hk_result_t hk_input_get_state(const hk_input_t *input, uint32_t *state)
{
    hk_result_t result;
    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = input_validate(input);
    return result == HK_OK ? input->get_state(input->context, state) : result;
}

hk_result_t hk_input_next_event(
    const hk_input_t *input, hk_input_cursor_t *cursor, hk_input_event_t *event)
{
    hk_result_t result;
    if(!cursor || !event)
        return HK_ERR_INVALID_ARGUMENT;
    memset(event, 0, sizeof(*event));
    result = input_validate(input);
    if(result != HK_OK)
        return result;
    if(!cursor->active)
        return HK_ERR_INVALID_STATE;
    return input->next_event(input->context, cursor, event);
}
