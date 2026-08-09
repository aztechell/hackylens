#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "micropython_binding_test_platform.h"
#include "micropython_binding_service.h"

typedef enum
{
    SCHEDULE_TICK_EACH_SLEEP = 0,
    SCHEDULE_TIMEOUT_BEFORE_TICK,
    SCHEDULE_UART_TICK_THEN_TIMEOUT,
    SCHEDULE_UART_DRAIN,
} test_schedule_t;

static i2c_t g_i2c;
volatile i2c_t *i2c[1] = {&g_i2c};

static uint64_t g_now_us;
static uint32_t g_run_id;
static uint32_t g_sleep_calls;
static uint32_t g_interrupt_poll;
static uint32_t g_interrupt_on_poll;
static uint32_t g_vm_hook_calls;
static uint32_t g_unwind_before_ack;
static uint32_t g_external_suspend_calls;
static uint32_t g_external_resume_calls;
static uint32_t g_illum_restore_calls;
static uint32_t g_rgb_restore_calls;
static uint32_t g_light_writes;
static uint32_t g_uart_init_calls;
static uint32_t g_overlay_acquire_calls;
static uint32_t g_overlay_present_calls;
static uint32_t g_overlay_release_calls;
static uint32_t g_overlay_acquired_run_id;
static uint32_t g_overlay_released_run_id;
static size_t g_overlay_command_count;
static size_t g_overlay_text_length;
static lcd_overlay_command_t
    g_overlay_commands[LCD_OVERLAY_COMMAND_MAX];
static uint8_t g_overlay_text[LCD_OVERLAY_TEXT_MAX];
static lcd_overlay_present_result_t g_overlay_present_result;
static uint8_t g_cleanup_events[8];
static size_t g_cleanup_event_count;
static size_t g_uart_budget;
static uint8_t g_uart_idle;
static uint8_t g_queued_pending_observed;
static uint8_t g_uart_bytes[1024];
static size_t g_uart_length;
static test_schedule_t g_schedule;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void reset_run(void)
{
    micropython_binding_service_cleanup();
    memset(&g_i2c, 0, sizeof(g_i2c));
    g_i2c.status = I2C_STATUS_TFNF | I2C_STATUS_TFE;
    g_now_us = 0U;
    g_sleep_calls = 0U;
    g_interrupt_poll = 0U;
    g_interrupt_on_poll = 0U;
    g_vm_hook_calls = 0U;
    g_unwind_before_ack = 0U;
    g_external_suspend_calls = 0U;
    g_external_resume_calls = 0U;
    g_illum_restore_calls = 0U;
    g_rgb_restore_calls = 0U;
    g_light_writes = 0U;
    g_uart_init_calls = 0U;
    g_overlay_acquire_calls = 0U;
    g_overlay_present_calls = 0U;
    g_overlay_release_calls = 0U;
    g_overlay_acquired_run_id = 0U;
    g_overlay_released_run_id = 0U;
    g_overlay_command_count = 0U;
    g_overlay_text_length = 0U;
    memset(g_overlay_commands, 0, sizeof(g_overlay_commands));
    memset(g_overlay_text, 0, sizeof(g_overlay_text));
    g_overlay_present_result = LCD_OVERLAY_PRESENT_OK;
    memset(g_cleanup_events, 0, sizeof(g_cleanup_events));
    g_cleanup_event_count = 0U;
    g_uart_budget = sizeof(g_uart_bytes);
    g_uart_idle = 1U;
    g_queued_pending_observed = 0U;
    g_uart_length = 0U;
    g_schedule = SCHEDULE_TICK_EACH_SLEEP;
    g_run_id++;
    micropython_binding_service_prepare(g_run_id);
}

static micropython_binding_result_t call_binding(
    micropython_binding_op_t operation, const uint32_t arguments[6],
    const uint8_t *input, size_t input_length)
{
    size_t output_length = 0U;

    return micropython_binding_call(operation, arguments,
                                    input, input_length,
                                    NULL, 0U, &output_length);
}

static void test_timeout_cancels_before_dispatch(void)
{
    uint32_t arguments[6] = {60U, 0U, 0U, 0U, 0U, 0U};
    micropython_binding_result_t result;

    reset_run();
    g_schedule = SCHEDULE_TIMEOUT_BEFORE_TICK;
    result = call_binding(MICROPYTHON_BINDING_OP_LED,
                          arguments, NULL, 0U);
    require_true(result == MICROPYTHON_BINDING_ERROR_TIMEOUT,
                 "RPC deadline must report timeout");
    require_true(g_light_writes == 0U,
                 "cancelled request must not dispatch a late light write");
    require_true(micropython_binding_service_test_cancel_acknowledged(),
                 "timeout must wait for a core-0 cancel acknowledgement");
    micropython_binding_service_tick();
    micropython_binding_service_tick();
    require_true(g_light_writes == 0U,
                 "acknowledged request must stay quiescent on later ticks");
}

static void test_stop_acknowledged_before_vm_unwind(void)
{
    static const uint8_t data[] = {'S', 'T', 'O', 'P'};
    uint32_t arguments[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    micropython_binding_result_t result;
    size_t stopped_length;

    reset_run();
    g_uart_budget = 1U;
    g_interrupt_on_poll = 2U;
    result = call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                          arguments, data, sizeof(data));
    require_true(result == MICROPYTHON_BINDING_ERROR_NOT_ACTIVE,
                 "host VM hook returns through the unreachable safety path");
    require_true(g_vm_hook_calls == 1U,
                 "pending stop must be delivered through the VM hook");
    require_true(g_unwind_before_ack == 0U,
                 "VM unwind must happen only after cancel acknowledgement");
    require_true(g_uart_length == 1U,
                 "stop fixture must interrupt an accepted UART request");
    stopped_length = g_uart_length;
    micropython_binding_service_tick();
    require_true(g_uart_length == stopped_length,
                 "stopped UART request must not progress after VM unwind");
}

static void test_uart_timeout_prevents_late_completion(void)
{
    static const uint8_t first[] = {'A', 'B', 'C', 'D'};
    static const uint8_t second[] = {'X', 'Y'};
    uint32_t arguments[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    micropython_binding_result_t result;
    size_t stopped_length;

    reset_run();
    g_uart_budget = 1U;
    g_schedule = SCHEDULE_UART_TICK_THEN_TIMEOUT;
    result = call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                          arguments, first, sizeof(first));
    require_true(result == MICROPYTHON_BINDING_ERROR_TIMEOUT,
                 "in-flight UART request must time out");
    require_true(g_uart_length == 1U && g_uart_bytes[0] == 'A',
                 "UART tick must make bounded partial progress");
    require_true(micropython_binding_service_test_cancel_acknowledged(),
                 "in-flight UART cancel must be acknowledged");
    stopped_length = g_uart_length;
    micropython_binding_service_tick();
    micropython_binding_service_tick();
    require_true(g_uart_length == stopped_length,
                 "UART must not write after cancellation completion");

    g_schedule = SCHEDULE_TICK_EACH_SLEEP;
    g_uart_budget = sizeof(g_uart_bytes);
    g_sleep_calls = 0U;
    result = call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                          arguments, second, sizeof(second));
    require_true(result == MICROPYTHON_BINDING_OK,
                 "acknowledged slot must be reusable");
    require_true(g_uart_length == 3U &&
                 g_uart_bytes[1] == 'X' && g_uart_bytes[2] == 'Y',
                 "next request must own an intact fresh context");
}

static void test_cleanup_is_idempotent(void)
{
    uint32_t uart_arguments[6] = {115200U, 0U, 0U, 0U, 0U, 0U};
    uint32_t led_arguments[6] = {50U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    require_true(call_binding(MICROPYTHON_BINDING_OP_UART_INIT,
                              uart_arguments, NULL, 0U) ==
                     MICROPYTHON_BINDING_OK,
                 "UART init fixture must succeed");
    require_true(call_binding(MICROPYTHON_BINDING_OP_LED,
                              led_arguments, NULL, 0U) ==
                     MICROPYTHON_BINDING_OK,
                 "LED fixture must succeed");
    micropython_binding_service_cleanup();
    micropython_binding_service_cleanup();
    require_true(g_external_suspend_calls == 1U &&
                 g_external_resume_calls == 1U,
                 "external connector lease must be restored exactly once");
    require_true(g_illum_restore_calls == 1U && g_rgb_restore_calls == 1U,
                 "light settings must be restored exactly once");
    require_true(g_cleanup_event_count == 4U &&
                 g_cleanup_events[0] == 1U &&
                 g_cleanup_events[1] == 2U &&
                 g_cleanup_events[2] == 3U &&
                 g_cleanup_events[3] == 4U,
                 "cleanup must follow external, lights, then display order");
}

static void test_uart_completion_waits_for_transmitter_drain(void)
{
    static const uint8_t data[] = {'D', 'R', 'N'};
    uint32_t arguments[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    g_uart_idle = 0U;
    g_schedule = SCHEDULE_UART_DRAIN;
    require_true(call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                              arguments, data, sizeof(data)) ==
                     MICROPYTHON_BINDING_OK,
                 "drained UART request must complete");
    require_true(g_uart_length == sizeof(data),
                 "drain fixture must queue every byte");
    require_true(g_queued_pending_observed,
                 "queue completion must remain pending until TX idle");
}

static void test_display_stages_until_present(void)
{
    static const uint8_t text[] = {'H', 'i'};
    uint32_t clear[6] = {0x1234U, 0U, 0U, 0U, 0U, 0U};
    uint32_t label[6] = {7U, 9U, 0xFFFFU, 0x0000U, 0U, 0U};
    uint32_t rect[6] = {11U, 13U, 17U, 19U, 0x07E0U, 1U};
    uint32_t none[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    require_true(g_overlay_acquire_calls == 1U &&
                 g_overlay_acquired_run_id == g_run_id,
                 "run prepare must acquire one display lease");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
                              clear, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "display clear must stage");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_TEXT,
                              label, text, sizeof(text)) ==
                     MICROPYTHON_BINDING_OK,
                 "display text must stage");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                              rect, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "display rectangle must stage");
    require_true(g_overlay_present_calls == 0U,
                 "staging commands must not write the panel");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "explicit display present must succeed");
    require_true(g_overlay_present_calls == 1U &&
                 g_overlay_command_count == 3U &&
                 g_overlay_text_length == sizeof(text),
                 "present must publish the complete staged frame");
    require_true(g_overlay_commands[0].type == LCD_OVERLAY_COMMAND_CLEAR &&
                 g_overlay_commands[0].color == 0x1234U,
                 "presented clear command must preserve its color");
    require_true(g_overlay_commands[1].type == LCD_OVERLAY_COMMAND_TEXT &&
                 g_overlay_commands[1].x == 7U &&
                 g_overlay_commands[1].y == 9U &&
                 !memcmp(g_overlay_text, text, sizeof(text)),
                 "presented text command and bytes must be intact");
    require_true(g_overlay_commands[2].type == LCD_OVERLAY_COMMAND_RECT &&
                 g_overlay_commands[2].width == 17U &&
                 g_overlay_commands[2].height == 19U &&
                 g_overlay_commands[2].filled,
                 "presented rectangle must preserve geometry");
}

static void test_display_cancel_preserves_stage_for_retry(void)
{
    static const uint8_t text[] = {'R', 'E', 'T', 'R', 'Y'};
    uint32_t clear[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    uint32_t label[6] = {1U, 2U, 0xFFFFU, 0U, 0U, 0U};
    uint32_t none[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
                              clear, NULL, 0U) == MICROPYTHON_BINDING_OK &&
                 call_binding(MICROPYTHON_BINDING_OP_DISPLAY_TEXT,
                              label, text, sizeof(text)) ==
                     MICROPYTHON_BINDING_OK,
                 "display retry fixture must stage a frame");
    g_overlay_present_result = LCD_OVERLAY_PRESENT_CANCELLED;
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) ==
                     MICROPYTHON_BINDING_ERROR_TIMEOUT,
                 "cancelled display transfer must report timeout");
    require_true(g_overlay_present_calls == 1U &&
                 g_overlay_command_count == 2U &&
                 !memcmp(g_overlay_text, text, sizeof(text)),
                 "cancelled present must receive an intact frame");
    g_overlay_present_result = LCD_OVERLAY_PRESENT_OK;
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "cancelled staged frame must be retryable");
    require_true(g_overlay_present_calls == 2U &&
                 g_overlay_command_count == 2U &&
                 !memcmp(g_overlay_text, text, sizeof(text)),
                 "retry must publish the same staged frame");
}

static void test_display_limit_clear_recovery_and_cleanup(void)
{
    uint32_t rect[6] = {1U, 1U, 2U, 2U, 0xFFFFU, 0U};
    uint32_t clear[6] = {0x0020U, 0U, 0U, 0U, 0U, 0U};
    uint32_t none[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    for(size_t i = 0U; i < LCD_OVERLAY_COMMAND_MAX; i++)
        require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                                  rect, NULL, 0U) ==
                         MICROPYTHON_BINDING_OK,
                     "bounded display command buffer must accept its limit");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                              rect, NULL, 0U) ==
                     MICROPYTHON_BINDING_ERROR_LIMIT,
                 "display command overflow must be explicit");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) ==
                     MICROPYTHON_BINDING_ERROR_LIMIT &&
                 g_overlay_present_calls == 0U,
                 "overflowed frame must never reach the panel driver");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
                              clear, NULL, 0U) == MICROPYTHON_BINDING_OK &&
                 call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "clear must recover from staging overflow");
    require_true(g_overlay_present_calls == 1U &&
                 g_overlay_command_count == 1U &&
                 g_overlay_commands[0].type == LCD_OVERLAY_COMMAND_CLEAR,
                 "recovered frame must contain only the new clear command");
    micropython_binding_service_cleanup();
    micropython_binding_service_cleanup();
    require_true(g_overlay_release_calls == 1U &&
                 g_overlay_released_run_id == g_run_id,
                 "display lease must be released exactly once for its run");
}

int main(void)
{
    test_timeout_cancels_before_dispatch();
    test_stop_acknowledged_before_vm_unwind();
    test_uart_timeout_prevents_late_completion();
    test_cleanup_is_idempotent();
    test_uart_completion_waits_for_transmitter_drain();
    test_display_stages_until_present();
    test_display_cancel_preserves_stage_for_retry();
    test_display_limit_clear_recovery_and_cleanup();
    micropython_binding_service_cleanup();
    puts("MICROPYTHON_BINDINGS_OK cases=8");
    return 0;
}

void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency)
{
    (void)device;
    (void)address;
    (void)address_width;
    (void)frequency;
}

int plic_irq_disable(int interrupt)
{
    (void)interrupt;
    return 0;
}

void external_link_service_suspend(void) { g_external_suspend_calls++; }
void external_link_service_resume(void)
{
    g_external_resume_calls++;
    g_cleanup_events[g_cleanup_event_count++] = 1U;
}
void illum_led_apply(void)
{
    g_illum_restore_calls++;
    g_cleanup_events[g_cleanup_event_count++] = 2U;
}
void rgb_led_apply(void)
{
    g_rgb_restore_calls++;
    g_cleanup_events[g_cleanup_event_count++] = 3U;
}
void board_external_link_i2c_pins(void) {}
uint32_t hk_input_state(void) { return 0x0aU; }

uint8_t lcd_overlay_acquire(uint32_t run_id)
{
    g_overlay_acquire_calls++;
    g_overlay_acquired_run_id = run_id;
    return run_id != 0U;
}

lcd_overlay_present_result_t lcd_overlay_present(
    uint32_t run_id, const lcd_overlay_command_t *commands,
    size_t command_count, const uint8_t *text, size_t text_length,
    lcd_overlay_cancel_fn cancelled, void *cancel_context)
{
    (void)cancelled;
    (void)cancel_context;
    require_true(run_id == g_overlay_acquired_run_id,
                 "present must use the acquired display run id");
    require_true(command_count <= LCD_OVERLAY_COMMAND_MAX &&
                 text_length <= LCD_OVERLAY_TEXT_MAX,
                 "service must respect bounded overlay capacities");
    require_true((command_count == 0U || commands) &&
                 (text_length == 0U || text),
                 "non-empty overlay buffers must be present");
    g_overlay_present_calls++;
    g_overlay_command_count = command_count;
    g_overlay_text_length = text_length;
    if(command_count)
        memcpy(g_overlay_commands, commands,
               command_count * sizeof(commands[0]));
    if(text_length)
        memcpy(g_overlay_text, text, text_length);
    return g_overlay_present_result;
}

uint8_t lcd_overlay_release(uint32_t run_id)
{
    g_overlay_release_calls++;
    g_overlay_released_run_id = run_id;
    g_cleanup_events[g_cleanup_event_count++] = 4U;
    return run_id == g_overlay_acquired_run_id;
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    (void)enabled;
    (void)brightness;
    g_light_writes++;
}

void lights_rgb_set(uint8_t enabled, uint8_t red,
                    uint8_t green, uint8_t blue)
{
    (void)enabled; (void)red; (void)green; (void)blue;
    g_light_writes++;
}

void hal_external_uart_init(uint32_t baud)
{
    (void)baud;
    g_uart_init_calls++;
}

size_t hal_external_uart_receive(uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
    return 0U;
}

size_t hal_external_uart_send_ready(const uint8_t *data, size_t length)
{
    size_t count = length;

    if(count > g_uart_budget)
        count = g_uart_budget;
    if(count > sizeof(g_uart_bytes) - g_uart_length)
        count = sizeof(g_uart_bytes) - g_uart_length;
    memcpy(g_uart_bytes + g_uart_length, data, count);
    g_uart_length += count;
    return count;
}

uint8_t hal_external_uart_tx_idle(void) { return g_uart_idle; }

uint64_t hal_time_us(void) { return g_now_us; }

void hal_sleep_ms(uint32_t duration_ms)
{
    g_sleep_calls++;
    if(g_schedule == SCHEDULE_TIMEOUT_BEFORE_TICK && g_sleep_calls == 1U)
    {
        g_now_us += 600000ULL;
        return;
    }
    if(g_schedule == SCHEDULE_UART_TICK_THEN_TIMEOUT && g_sleep_calls == 1U)
    {
        micropython_binding_service_tick();
        g_now_us += 4000000ULL;
        return;
    }
    if(g_schedule == SCHEDULE_UART_DRAIN)
    {
        if(g_sleep_calls == 1U)
        {
            micropython_binding_service_tick();
            if(g_uart_length != 0U &&
               micropython_binding_service_test_request_pending())
                g_queued_pending_observed = 1U;
        }
        else
        {
            g_uart_idle = 1U;
            micropython_binding_service_tick();
        }
        g_now_us += (uint64_t)duration_ms * 1000ULL;
        return;
    }
    micropython_binding_service_tick();
    g_now_us += (uint64_t)duration_ms * 1000ULL;
}

uint8_t micropython_runtime_interrupt_pending(void)
{
    g_interrupt_poll++;
    return g_interrupt_on_poll != 0U &&
           g_interrupt_poll >= g_interrupt_on_poll;
}

void micropython_runtime_vm_hook(void)
{
    g_vm_hook_calls++;
    if(!micropython_binding_service_test_cancel_acknowledged())
        g_unwind_before_ack++;
}
