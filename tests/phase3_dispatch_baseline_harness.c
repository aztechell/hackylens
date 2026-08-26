#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "../firmware/src/core/hk_app_registry.h"
#include "../firmware/src/core/hk_dispatch.h"
#include "../firmware/src/core/hk_menu.h"
#include "../firmware/src/core/hk_screen.h"

#define SAMPLE_COUNT 101U
#define ITERATIONS_PER_SAMPLE 1000U
#define DISPATCH_P99_LIMIT_NS 100000ULL

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if(!(expression))                                                     \
        {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #expression);                                             \
            exit(1);                                                          \
        }                                                                     \
    } while(0)

static volatile uint32_t s_callback_count;
static screen_t s_screen = SCREEN_BUTTONS;

static void measured_callback(const hk_input_snapshot_t *input)
{
    s_callback_count += input != NULL ? 1U : 0U;
}

static const hk_app_t s_app = {
    .id = "baseline",
    .title = "BASELINE",
    .screen = SCREEN_BUTTONS,
    .handle_input = measured_callback,
};

screen_t hk_screen_get(void)
{
    return s_screen;
}

const hk_app_t *hk_app_for_screen(screen_t screen)
{
    return screen == s_app.screen ? &s_app : NULL;
}

void menu_select_delta(int8_t delta)
{
    (void)delta;
}

void menu_repeat_start(uint32_t button)
{
    (void)button;
}

void shell_open_selected(const hk_input_snapshot_t *input)
{
    (void)input;
}

void menu_select_vertical(void)
{
}

static uint64_t monotonic_ns(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    CHECK(QueryPerformanceFrequency(&frequency) != 0);
    CHECK(QueryPerformanceCounter(&counter) != 0);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000000ULL +
           (uint64_t)((counter.QuadPart % frequency.QuadPart) * 1000000000ULL /
                      frequency.QuadPart);
#elif defined(CLOCK_MONOTONIC)
    struct timespec value;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#else
    struct timespec value;
    CHECK(timespec_get(&value, TIME_UTC) == TIME_UTC);
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#endif
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t first = *(const uint64_t *)left;
    uint64_t second = *(const uint64_t *)right;
    return first < second ? -1 : first > second ? 1 : 0;
}

int main(void)
{
    hk_input_snapshot_t input = {0U, 0U, 0U};
    uint64_t samples[SAMPLE_COUNT];

    for(uint32_t sample = 0U; sample < SAMPLE_COUNT; sample++)
    {
        uint64_t start = monotonic_ns();
        for(uint32_t iteration = 0U; iteration < ITERATIONS_PER_SAMPLE;
            iteration++)
        {
            shell_handle_buttons(&input);
        }
        samples[sample] = (monotonic_ns() - start) / ITERATIONS_PER_SAMPLE;
    }

    CHECK(s_callback_count == SAMPLE_COUNT * ITERATIONS_PER_SAMPLE);
    qsort(samples, SAMPLE_COUNT, sizeof(samples[0]), compare_u64);
    CHECK(samples[99U] <= DISPATCH_P99_LIMIT_NS);
    printf("PHASE3_DISPATCH_BASELINE_OK host_p99_ns=%llu limit_us=100 "
           "samples=%u iterations=%u\n",
           (unsigned long long)samples[99U], SAMPLE_COUNT,
           ITERATIONS_PER_SAMPLE);
    return 0;
}
