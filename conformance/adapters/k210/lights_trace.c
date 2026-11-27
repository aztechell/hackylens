#include <hackylens/capability/lights.h>
#include "lights_normative_backend.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *outcome(hk_result_t result)
{
    switch(result)
    {
    case HK_OK: return "success";
    case HK_ERR_BUSY: return "conflict";
    case HK_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case HK_ERR_INVALID_STATE: return "invalid_state";
    case HK_ERR_WRONG_OWNER:
    case HK_ERR_STALE_HANDLE: return "authority_denied";
    case HK_ERR_DEADLINE_EXCEEDED: return "deadline";
    default: return "unexpected_result";
    }
}

int main(int argc, char **argv)
{
    const hk_lights_service_t *service = hk_lights_service();
    hk_lights_t sessions[4] = {0};
    lights_normative_backend_reset(100);
    for(int i = 1; i < argc; ++i)
    {
        char action[12];
        unsigned int slot, channel, level;
        uint64_t deadline_at_us;
        int fields = sscanf(argv[i], "%11[^:]:%u:%u:%u:%" SCNu64,
                            action, &slot, &channel, &level, &deadline_at_us);
        if(fields != 5 || slot >= 4 || level > UINT16_MAX)
            return 2;
        hk_lights_t *session = &sessions[slot];
        hk_deadline_t deadline = {deadline_at_us};
        hk_result_t result;
        if(strcmp(action, "claim") == 0)
            result = hk_lights_open(service, channel, session);
        else if(strcmp(action, "copy") == 0 && channel < 4)
        {
            *session = sessions[channel];
            result = HK_OK;
        }
        else if(strcmp(action, "set") == 0)
            result = hk_lights_set_level(session, channel, (uint16_t)level, deadline, NULL);
        else if(strcmp(action, "close") == 0)
            result = hk_lights_close(session, deadline);
        else if(strcmp(action, "retire") == 0)
            result = hk_lights_retire(session, deadline);
        else
            return 2;
        printf("{\"event\":\"lights\",\"action\":\"%s\",\"outcome\":\"%s\","
               "\"effects\":%" PRIu32 ",\"active\":%" PRIu32 ",\"safe_off\":%" PRIu32 "}\n",
               argv[i], outcome(result), lights_normative_backend_effect_count(),
               lights_normative_backend_active_mask(), lights_normative_backend_safe_off_mask());
    }
    return 0;
}
