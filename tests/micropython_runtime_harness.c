#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "micropython_runtime.h"
#include "core1_executor.h"
#include "../firmware/src/adapters/micropython/micropython_capability_bridge.h"
#include "stub_micropython.h"

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr, "FAIL line=%d: %s\n", __LINE__, #condition); \
    exit(1); } } while(0)

nlr_buf_t *s_test_nlr;
int mp_plat_print;
int mp_type_KeyboardInterrupt;
static core1_executor_job_fn s_job;
static void *s_job_context;
static uint32_t s_next_ticket = 1U;
static uint8_t s_job_complete;
static uint8_t s_throw_exception;
static uint8_t s_probe_running;
static uint8_t s_finish_on_clock;
static uint8_t s_check_interrupt;
static uint32_t s_prepare_count;
static uint32_t s_cleanup_count;
static uint32_t s_wake_count;
static const char *s_expected_source;
static uint64_t s_now_us = 1000U;

static void run_worker(void);

uint8_t core1_executor_init(void) { return 1U; }
uint8_t core1_executor_idle(void) { return !s_job || s_job_complete; }
uint32_t core1_executor_submit(core1_executor_job_fn job, void *context)
{
    CHECK(job != NULL);
    s_job = job;
    s_job_context = context;
    s_job_complete = 0U;
    return s_next_ticket++;
}
uint8_t core1_executor_complete(uint32_t ticket)
{
    CHECK(ticket != 0U);
    return s_job_complete;
}
void micropython_capability_bridge_prepare(uint32_t run_id)
{
    CHECK(run_id == s_prepare_count + 1U);
    s_prepare_count++;
}
hk_result_t micropython_capability_bridge_cleanup(void)
{
    s_cleanup_count++;
    return HK_OK;
}
void hk_screen_request_wake(void) { s_wake_count++; }
uint64_t hal_time_us(void)
{
    if(s_finish_on_clock)
    {
        s_finish_on_clock = 0U;
        run_worker();
    }
    return s_now_us;
}
void hal_watchdog_force_reset(uint64_t timeout_ms)
{
    (void)timeout_ms;
    CHECK(0);
}

void mp_embed_init(void *heap, size_t bytes, void *stack_anchor)
{
    CHECK(heap != NULL && bytes != 0U && stack_anchor != NULL);
}
void mp_embed_deinit(void) {}
void mp_stack_set_limit(size_t bytes) { CHECK(bytes != 0U); }
mp_lexer_t *mp_lexer_new_from_str_len(qstr name, const char *source,
    size_t length, uint32_t flags)
{
    static mp_lexer_t lexer;
    CHECK(name == MP_QSTR__lt_stdin_gt_ && flags == 0U);
    CHECK(strlen(s_expected_source) == length);
    CHECK(memcmp(source, s_expected_source, length) == 0);
    lexer.source_name = name;
    return &lexer;
}
mp_parse_tree_t mp_parse(mp_lexer_t *lexer, int mode)
{
    CHECK(lexer != NULL && mode == MP_PARSE_FILE_INPUT);
    return (mp_parse_tree_t){0};
}
mp_obj_t mp_compile(mp_parse_tree_t *tree, qstr source_name, int is_repl)
{
    CHECK(tree != NULL && source_name == MP_QSTR__lt_stdin_gt_ && is_repl);
    return (mp_obj_t)(uintptr_t)1U;
}
mp_obj_t mp_call_function_0(mp_obj_t module)
{
    micropython_runtime_status_t before = {0}, after = {0};
    CHECK(module != NULL);
    micropython_runtime_stdout_write("ok", 2U);
    if(s_probe_running)
    {
        micropython_runtime_get_status(&before);
        CHECK(before.state == MICROPYTHON_RUNTIME_RUNNING);
        CHECK(before.exit_reason == MICROPYTHON_EXIT_NONE);
        CHECK(before.output_pending == 2U);
        CHECK(micropython_runtime_start("replacement", 11U, 1U) == 0U);
        CHECK(micropython_runtime_start(NULL, 0U, 1U) == 0U);
        CHECK(micropython_runtime_start("x",
            MICROPYTHON_RUNTIME_SOURCE_MAX + 1U, 1U) == 0U);
        micropython_runtime_poll();
        micropython_runtime_get_status(&after);
        CHECK(memcmp(&before, &after, sizeof(before)) == 0);
        CHECK(s_cleanup_count + 1U == s_prepare_count);
    }
    if(s_throw_exception)
    {
        CHECK(s_test_nlr != NULL);
        s_test_nlr->ret_val = (void *)(uintptr_t)1U;
        longjmp(s_test_nlr->jump, 1);
    }
    if(s_check_interrupt)
        micropython_runtime_vm_hook();
    return module;
}
void mp_obj_print_exception(const int *print, mp_obj_t exception)
{
    CHECK(print == &mp_plat_print && exception != NULL);
    micropython_runtime_stdout_write("E", 1U);
}
void mp_raise_type(const int *type)
{
    CHECK(type == &mp_type_KeyboardInterrupt);
    CHECK(s_test_nlr != NULL);
    s_test_nlr->ret_val = (void *)(uintptr_t)1U;
    longjmp(s_test_nlr->jump, 1);
}

static void run_worker(void)
{
    CHECK(s_job != NULL && !s_job_complete);
    s_job(s_job_context);
    s_job_complete = 1U;
}

int main(void)
{
    micropython_runtime_status_t before = {0}, after = {0};

    s_expected_source = "A";
    CHECK(micropython_runtime_start("A", 1U, 1000U) == 1U);
    micropython_runtime_get_status(&before);
    CHECK(before.state == MICROPYTHON_RUNTIME_STARTING);
    CHECK(before.exit_reason == MICROPYTHON_EXIT_NONE);
    CHECK(before.run_id == 1U && before.source_bytes == 1U);
    CHECK(micropython_runtime_start("B", 1U, 1000U) == 0U);
    CHECK(micropython_runtime_start(NULL, 0U, 1000U) == 0U);
    micropython_runtime_poll();
    micropython_runtime_get_status(&after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    CHECK(s_cleanup_count == 0U && s_prepare_count == 1U);
    s_probe_running = 1U;
    run_worker();
    micropython_runtime_get_status(&after);
    CHECK(after.state == MICROPYTHON_RUNTIME_FINISHED);
    CHECK(after.exit_reason == MICROPYTHON_EXIT_COMPLETE);
    CHECK(after.run_id == 1U && after.output_pending == 2U);
    CHECK(s_cleanup_count == 0U);
    micropython_runtime_poll();
    CHECK(s_cleanup_count == 1U);

    s_expected_source = "C";
    s_throw_exception = 1U;
    CHECK(micropython_runtime_start("C", 1U, 1000U) == 1U);
    micropython_runtime_get_status(&before);
    CHECK(before.state == MICROPYTHON_RUNTIME_STARTING);
    CHECK(before.exit_reason == MICROPYTHON_EXIT_NONE);
    CHECK(before.run_id == 2U && before.output_pending == 0U);
    CHECK(micropython_runtime_start("D", 1U, 1000U) == 0U);
    CHECK(micropython_runtime_start(NULL, 0U, 1000U) == 0U);
    micropython_runtime_poll();
    micropython_runtime_get_status(&after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    CHECK(s_cleanup_count == 1U && s_prepare_count == 2U);
    run_worker();
    micropython_runtime_get_status(&after);
    CHECK(after.state == MICROPYTHON_RUNTIME_ERROR);
    CHECK(after.exit_reason == MICROPYTHON_EXIT_EXCEPTION);
    CHECK(after.run_id == 2U && after.output_pending == 3U);
    CHECK(s_cleanup_count == 1U);
    micropython_runtime_poll();
    CHECK(s_cleanup_count == 2U);

    CHECK(micropython_runtime_start(NULL, 0U, 1000U) == 0U);
    micropython_runtime_get_status(&after);
    CHECK(after.state == MICROPYTHON_RUNTIME_ERROR);
    CHECK(after.exit_reason == MICROPYTHON_EXIT_INVALID_SOURCE);
    CHECK(after.run_id == 2U);
    CHECK(s_wake_count == 2U);

    s_probe_running = 0U;
    s_throw_exception = 0U;
    s_expected_source = "stop";
    CHECK(micropython_runtime_start("stop", 4U, 1000U) == 1U);
    CHECK(micropython_runtime_request_stop() == 1U);
    micropython_runtime_get_status(&after);
    CHECK(after.state == MICROPYTHON_RUNTIME_STOPPING);
    s_check_interrupt = 1U;
    run_worker();
    micropython_runtime_poll();
    micropython_runtime_get_status(&after);
    CHECK(after.state == MICROPYTHON_RUNTIME_STOPPED);
    CHECK(after.exit_reason == MICROPYTHON_EXIT_REQUESTED && s_cleanup_count == 3U);

    s_check_interrupt = 0U;
    s_expected_source = "finish";
    CHECK(micropython_runtime_start("finish", 6U, 1000U) == 1U);
    s_finish_on_clock = 1U;
    CHECK(micropython_runtime_request_stop() == 1U);
    micropython_runtime_poll();
    micropython_runtime_get_status(&after);
    CHECK(after.state == MICROPYTHON_RUNTIME_FINISHED);
    CHECK(after.exit_reason == MICROPYTHON_EXIT_COMPLETE && s_cleanup_count == 4U);

    s_expected_source = "local";
    CHECK(micropython_runtime_start("local", 5U, MICROPYTHON_RUNTIME_UNTIL_STOPPED));
    micropython_runtime_get_status(&after);
    CHECK(after.deadline_us == 0U);
    s_now_us += 3600000000ULL;
    micropython_runtime_poll();
    CHECK(!micropython_runtime_interrupt_pending());
    CHECK(micropython_runtime_request_stop());
    s_check_interrupt = 1U;
    run_worker();
    micropython_runtime_poll();
    micropython_runtime_get_status(&after);
    CHECK(after.exit_reason == MICROPYTHON_EXIT_REQUESTED && s_cleanup_count == 5U);
    puts("MICROPYTHON_RUNTIME_OK busy=2 invalid_active=2 complete=2 exception=1 stop=2 cleanup=5");
    return 0;
}
