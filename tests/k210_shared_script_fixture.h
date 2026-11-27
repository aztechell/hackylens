#ifndef HK_TEST_K210_SHARED_SCRIPT_FIXTURE_H
#define HK_TEST_K210_SHARED_SCRIPT_FIXTURE_H

#include <stdint.h>

typedef enum
{
    HK_TEST_SCRIPT_DISPATCH,
    HK_TEST_SCRIPT_TIMEOUT_BEFORE_DISPATCH,
    HK_TEST_SCRIPT_STOP_BEFORE_DISPATCH,
} hk_test_script_schedule_t;

uint32_t hk_test_k210_script_prepare(void);
void hk_test_k210_script_schedule(hk_test_script_schedule_t schedule);
uint32_t hk_test_k210_script_unwind_calls(void);
uint32_t hk_test_k210_script_unwind_before_ack(void);
void hk_test_k210_script_before_dispatch(void);
int hk_test_k210_script_trace(int argc, char **argv);

#endif
