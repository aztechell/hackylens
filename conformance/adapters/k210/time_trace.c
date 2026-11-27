#include <hackylens/capability/time.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-side replacements for the board clock, sleep and test lock hooks. */
unsigned int g_test_lock_depth;
unsigned int g_test_lock_calls;
unsigned int g_test_unlock_calls;
unsigned int g_test_lock_violations;

static uint64_t now_us;
static uint64_t cancel_at_us;
static int freeze_clock;

uint64_t hal_time_us(void)
{
    printf("{\"event\":\"clock\",\"now_us\":%" PRIu64 "}\n", now_us);
    return now_us;
}

void usleep(uint64_t duration_us)
{
    printf("{\"event\":\"wait\",\"now_us\":%" PRIu64
           ",\"duration_us\":%" PRIu64 "}\n", now_us, duration_us);
    if(!freeze_clock)
        now_us += duration_us;
}

static uint8_t cancel_probe(const void *context)
{
    (void)context;
    uint8_t active = now_us >= cancel_at_us;
    printf("{\"event\":\"cancel_probe\",\"now_us\":%" PRIu64
           ",\"active\":%s}\n", now_us, active ? "true" : "false");
    return active;
}

static const char *outcome(hk_result_t result)
{
    switch(result)
    {
    case HK_OK: return "success";
    case HK_ERR_CANCELLED: return "cancelled";
    case HK_ERR_DEADLINE_EXCEEDED: return "deadline";
    case HK_ERR_INTERNAL: return "clock_failure";
    case HK_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case HK_ERR_LIMIT: return "limit";
    default: return "unexpected_result";
    }
}

static uint64_t fixture_number(const char *argument)
{
    return (uint64_t)strtoumax(argument, NULL, 10);
}

static void print_value(const char *event, hk_result_t result, uint64_t value)
{
    printf("{\"event\":\"%s\",\"outcome\":\"%s\",\"value\":", event, outcome(result));
    if(result == HK_OK)
        printf("%" PRIu64, value);
    else
        fputs("null", stdout);
    puts("}");
}

int main(int argc, char **argv)
{
    const hk_time_t *time = hk_time_service();
    if(argc == 7 && strcmp(argv[1], "sleep") == 0)
    {
        now_us = fixture_number(argv[2]);
        hk_deadline_t target = {fixture_number(argv[3])};
        hk_deadline_t deadline = {fixture_number(argv[4])};
        hk_cancel_t cancel = {cancel_probe, NULL};
        int has_cancel = strcmp(argv[5], "none") != 0;
        if(has_cancel)
            cancel_at_us = fixture_number(argv[5]);
        freeze_clock = strcmp(argv[6], "freeze") == 0;
        hk_result_t result = hk_time_sleep_until(time, target, deadline,
                                                has_cancel ? &cancel : NULL);
        printf("{\"event\":\"sleep_result\",\"outcome\":\"%s\",\"now_us\":%" PRIu64
               "}\n", outcome(result), now_us);
        return 0;
    }
    if(argc == 4 && strcmp(argv[1], "deadline") == 0)
    {
        now_us = fixture_number(argv[2]);
        hk_deadline_t value = {0};
        hk_result_t result = hk_time_deadline_after_us(time, fixture_number(argv[3]), &value);
        print_value("deadline_result", result, value.at_us);
        return 0;
    }
    if(argc >= 3 && strcmp(argv[1], "read") == 0)
    {
        for(int i = 2; i < argc; ++i)
        {
            now_us = fixture_number(argv[i]);
            uint64_t value = 0;
            hk_result_t result = hk_time_now_us(time, &value);
            print_value("read_result", result, value);
        }
        return 0;
    }
    fputs("unrecognized Time fixture command\n", stderr);
    return 2;
}
