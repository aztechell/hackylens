#ifndef HK_TEST_K210_SCRIPT_RUNTIME_FIXTURE_H
#define HK_TEST_K210_SCRIPT_RUNTIME_FIXTURE_H

#include <stdint.h>

typedef enum
{
    HK_TEST_VM_COMPLETES,
    HK_TEST_VM_THROWS,
    HK_TEST_VM_CHECKS_INTERRUPT,
} hk_test_vm_behavior_t;

void hk_test_vm_configure(const char *source, hk_test_vm_behavior_t behavior);
void hk_test_vm_return_from_worker(void);
void hk_test_vm_acknowledge_executor(void);
void hk_test_vm_set_clock(uint64_t now_us);
uint8_t hk_test_vm_executor_acknowledged(void);
uint32_t hk_test_vm_cleanup_calls(void);
uint32_t hk_test_vm_prepared_runs(void);
int hk_test_vm_runtime_trace(int argc, char **argv);

#endif
