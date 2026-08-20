#include "micropython_binding_service.h"

#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/lights.h>

#if defined(MICROPYTHON_BINDING_TESTING)
#include "micropython_binding_test_platform.h"
#else
#include <hackylens/capability/input.h>
#include <i2c.h>
#include <plic.h>

#include "external_link_service.h"
#include "micropython_runtime.h"
#include "settings_lights.h"
#include "../capabilities/capability_client_binding.h"
#include "../internal/hk_board_port.h"
#include "../config/display_config.h"
#include "../drivers/hk_lcd.h"
#include "hal_external_link.h"
#include "hal_time.h"
#endif

#if defined(MICROPYTHON_BINDING_TESTING)
#define BINDING_PREPARE_EXTERNAL_I2C() board_external_link_i2c_pins()
#else
#define BINDING_PREPARE_EXTERNAL_I2C() hk_board_ops.external_i2c_prepare()
#endif

#define MICROPYTHON_BINDING_RPC_TIMEOUT_US 500000ULL
#define MICROPYTHON_BINDING_I2C_TIMEOUT_US 100000ULL
#define MICROPYTHON_BINDING_I2C_HZ 100000U
#define MICROPYTHON_BINDING_UART_BITS_PER_BYTE 10ULL
#define MICROPYTHON_BINDING_UART_MIN_BAUD 1200ULL

#if !defined(MICROPYTHON_BINDING_TESTING)
static hk_input_t s_binding_input;
static hk_owner_t s_binding_input_owner;

static hk_result_t binding_input_state(uint32_t *state)
{
    static const hk_capability_request_t request = HK_INPUT_REQUEST_0_1_INIT;
    hk_owner_t owner = capability_client_consumer_owner(
        "consumer:micropython-adapter");
    hk_result_t result;

    if(hk_owner_is_zero(owner))
        return HK_ERR_STALE_HANDLE;
    if(owner.slot != s_binding_input_owner.slot ||
       owner.generation != s_binding_input_owner.generation ||
       hk_lease_is_zero(&s_binding_input.lease))
    {
        s_binding_input.lease = HK_LEASE_NONE;
        s_binding_input_owner = owner;
        result = hk_input_acquire(owner, &request, &s_binding_input);
        if(result != HK_OK)
            return result;
    }
    return hk_input_get_state(owner, &s_binding_input, state);
}
#endif

typedef enum
{
    BINDING_EXTERNAL_NONE = 0,
    BINDING_EXTERNAL_UART,
    BINDING_EXTERNAL_I2C,
} binding_external_mode_t;

typedef struct
{
    volatile uint32_t run_active;
    volatile uint32_t run_id;
    volatile uint32_t request_ticket;
    volatile uint32_t accepted_ticket;
    volatile uint32_t cancel_ticket;
    volatile uint32_t cancel_ack_ticket;
    volatile uint32_t complete_ticket;
    volatile uint32_t operation;
    volatile uint32_t arguments[6];
    volatile uint32_t input_length;
    volatile uint32_t output_length;
    volatile uint32_t result;
    volatile uint8_t data[MICROPYTHON_BINDING_DATA_MAX];
} micropython_binding_control_t;

typedef struct
{
    uint32_t ticket;
    uint32_t run_id;
    size_t length;
    size_t position;
    uint8_t data[MICROPYTHON_BINDING_DATA_MAX];
    uint8_t active;
} binding_uart_write_t;

typedef struct
{
    lcd_overlay_command_t commands[LCD_OVERLAY_COMMAND_MAX];
    uint8_t text[LCD_OVERLAY_TEXT_MAX];
    size_t command_count;
    size_t text_length;
    uint32_t run_id;
    uint8_t overflowed;
} binding_display_stage_t;

typedef struct
{
    uint32_t ticket;
    uint32_t run_id;
    size_t command_count;
    size_t text_length;
    uint8_t overflowed;
    uint8_t saved_first;
    lcd_overlay_command_t first_command;
    uint8_t active;
} binding_display_transaction_t;

typedef struct
{
    micropython_binding_control_t *control;
    uint32_t ticket;
    uint32_t run_id;
} binding_display_cancel_context_t;

static micropython_binding_control_t g_control_storage
    __attribute__((aligned(64)));
static uint8_t g_external_owned;
static uint32_t g_lights_owned;
static hk_lights_t g_illumination_lights;
static hk_owner_t g_illumination_owner;
static hk_lights_t g_rgb_lights;
static hk_owner_t g_rgb_owner;
static binding_external_mode_t g_external_mode;
static uint32_t g_uart_baud = 115200U;
static binding_uart_write_t g_uart_write;
static binding_display_stage_t g_display_stage;
static binding_display_transaction_t g_display_transaction;
static uint8_t g_display_owned;

static micropython_binding_control_t *binding_control(void)
{
    uintptr_t address = (uintptr_t)&g_control_storage;

    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (micropython_binding_control_t *)address;
}

static uint8_t binding_request_cancelled(
    const micropython_binding_control_t *control,
    uint32_t ticket, uint32_t run_id)
{
    return !control->run_active || control->run_id != run_id ||
           control->cancel_ticket == ticket;
}

static uint8_t binding_display_cancelled(void *context)
{
    const binding_display_cancel_context_t *cancel_context =
        (const binding_display_cancel_context_t *)context;

    return !cancel_context || binding_request_cancelled(
        cancel_context->control, cancel_context->ticket,
        cancel_context->run_id);
}

static uint8_t binding_lights_cancelled(const void *context)
{
    const binding_display_cancel_context_t *cancel_context =
        (const binding_display_cancel_context_t *)context;

    return !cancel_context || binding_request_cancelled(
        cancel_context->control, cancel_context->ticket,
        cancel_context->run_id);
}

static hk_result_t binding_claim_light(
    const char *consumer_id, uint32_t channel, uint64_t feature,
    hk_lights_t *handle, hk_owner_t *owner)
{
    hk_capability_request_t request = HK_LIGHTS_REQUEST_0_1_INIT;
    hk_owner_t candidate;
    hk_result_t result;

    if(!handle || !owner)
        return HK_ERR_INVALID_ARGUMENT;
    if(!hk_lease_is_zero(&handle->lease))
        return HK_OK;
    candidate = capability_client_consumer_owner(consumer_id);
    if(hk_owner_is_zero(candidate))
        return HK_ERR_STALE_HANDLE;
    request.required_features = feature;
    settings_lights_suspend(channel);
    result = hk_lights_acquire(candidate, &request, channel, handle);
    if(result != HK_OK)
    {
        settings_lights_restore(channel);
        return result;
    }
    *owner = candidate;
    g_lights_owned |= channel;
    return HK_OK;
}

static micropython_binding_result_t binding_lights_result(hk_result_t result)
{
    if(result == HK_OK)
        return MICROPYTHON_BINDING_OK;
    if(result == HK_ERR_INVALID_ARGUMENT || result == HK_ERR_LIMIT)
        return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
    if(result == HK_ERR_CANCELLED || result == HK_ERR_DEADLINE_EXCEEDED)
        return MICROPYTHON_BINDING_ERROR_TIMEOUT;
    if(result == HK_ERR_BUSY)
        return MICROPYTHON_BINDING_ERROR_BUSY;
    return MICROPYTHON_BINDING_ERROR_IO;
}

static uint16_t binding_rgb_level(uint32_t value)
{
    return (uint16_t)((value * 1000U + 127U) / 255U);
}

static void binding_display_stage_reset(uint32_t run_id)
{
    g_display_stage.command_count = 0U;
    g_display_stage.text_length = 0U;
    g_display_stage.run_id = run_id;
    g_display_stage.overflowed = 0U;
    g_display_transaction.active = 0U;
}

static void binding_display_transaction_begin(uint32_t ticket,
                                              uint32_t run_id,
                                              uint8_t replaces_first)
{
    g_display_transaction.ticket = ticket;
    g_display_transaction.run_id = run_id;
    g_display_transaction.command_count = g_display_stage.command_count;
    g_display_transaction.text_length = g_display_stage.text_length;
    g_display_transaction.overflowed = g_display_stage.overflowed;
    g_display_transaction.saved_first =
        replaces_first && g_display_stage.command_count != 0U;
    if(g_display_transaction.saved_first)
        g_display_transaction.first_command = g_display_stage.commands[0];
    g_display_transaction.active = 1U;
}

static void binding_display_transaction_finish(uint32_t ticket,
                                               uint32_t run_id,
                                               uint8_t rollback)
{
    if(!g_display_transaction.active ||
       g_display_transaction.ticket != ticket ||
       g_display_transaction.run_id != run_id)
        return;
    if(rollback)
    {
        if(g_display_transaction.saved_first)
            g_display_stage.commands[0] =
                g_display_transaction.first_command;
        g_display_stage.command_count =
            g_display_transaction.command_count;
        g_display_stage.text_length = g_display_transaction.text_length;
        g_display_stage.overflowed = g_display_transaction.overflowed;
    }
    g_display_transaction.active = 0U;
}

static micropython_binding_result_t binding_display_append(
    const lcd_overlay_command_t *command, const uint8_t *text,
    size_t text_length)
{
    lcd_overlay_command_t staged;

    if(!command || (text_length && !text))
        return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
    if(g_display_stage.command_count >= LCD_OVERLAY_COMMAND_MAX ||
       text_length > LCD_OVERLAY_TEXT_MAX - g_display_stage.text_length)
    {
        g_display_stage.overflowed = 1U;
        return MICROPYTHON_BINDING_ERROR_LIMIT;
    }
    staged = *command;
    if(text_length)
    {
        staged.text_offset = (uint16_t)g_display_stage.text_length;
        staged.text_length = (uint16_t)text_length;
        for(size_t i = 0U; i < text_length; i++)
            g_display_stage.text[g_display_stage.text_length + i] = text[i];
        g_display_stage.text_length += text_length;
    }
    g_display_stage.commands[g_display_stage.command_count++] = staged;
    return MICROPYTHON_BINDING_OK;
}

static void binding_complete_request(
    micropython_binding_control_t *control, uint32_t ticket,
    micropython_binding_result_t result, uint8_t cancelled)
{
    control->result = result;
    if(cancelled)
    {
        control->output_length = 0U;
        control->cancel_ack_ticket = ticket;
    }
    __sync_synchronize();
    control->complete_ticket = ticket;
}

static uint64_t binding_rpc_timeout_us(
    micropython_binding_op_t operation, size_t input_length)
{
    uint64_t timeout = MICROPYTHON_BINDING_RPC_TIMEOUT_US;

    if(operation == MICROPYTHON_BINDING_OP_UART_WRITE)
    {
        uint64_t wire_us =
            ((uint64_t)input_length * MICROPYTHON_BINDING_UART_BITS_PER_BYTE *
             1000000ULL + MICROPYTHON_BINDING_UART_MIN_BAUD - 1ULL) /
            MICROPYTHON_BINDING_UART_MIN_BAUD;
        timeout += wire_us;
    }
    return timeout;
}

static void binding_cancel_and_wait(
    micropython_binding_control_t *control, uint32_t ticket)
{
    /* The shared request buffer is single-slot storage.  Keep its lifetime
     * until core 0 publishes completion, which is also the final cancel ack.
     * Core-0 operations are bounded or incremental, so this wait cannot pin
     * the system tick behind a blocking peripheral call. */
    control->cancel_ticket = ticket;
    __sync_synchronize();
    while(control->complete_ticket != ticket)
        hal_sleep_ms(1U);
    __sync_synchronize();
}

static void binding_i2c_stop(void)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];

    adapter->intr_mask = 0U;
    adapter->enable = 0U;
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
}

static void binding_claim_external(void)
{
    if(g_external_owned)
        return;
    external_link_service_suspend();
    g_external_owned = 1U;
}

static void binding_select_uart(uint32_t baud)
{
    binding_claim_external();
    if(g_external_mode == BINDING_EXTERNAL_I2C)
        binding_i2c_stop();
    if(g_external_mode != BINDING_EXTERNAL_UART || g_uart_baud != baud)
        hal_external_uart_init(baud);
    g_uart_baud = baud;
    g_external_mode = BINDING_EXTERNAL_UART;
}

static void binding_select_i2c(uint8_t address)
{
    binding_claim_external();
    if(g_external_mode == BINDING_EXTERNAL_I2C)
        binding_i2c_stop();
    BINDING_PREPARE_EXTERNAL_I2C();
    i2c_init(I2C_DEVICE_0, address, 7U, MICROPYTHON_BINDING_I2C_HZ);
    g_external_mode = BINDING_EXTERNAL_I2C;
}

static uint8_t binding_i2c_timed_out(uint64_t deadline)
{
    return hal_time_us() >= deadline;
}

static int binding_i2c_write(micropython_binding_control_t *control,
                             uint32_t ticket, uint32_t run_id,
                             uint8_t address, const uint8_t *data,
                             size_t length)
{
    volatile i2c_t *adapter;
    uint64_t deadline = hal_time_us() + MICROPYTHON_BINDING_I2C_TIMEOUT_US;
    size_t position = 0U;

    if(binding_request_cancelled(control, ticket, run_id))
        return -3;
    binding_select_i2c(address);
    adapter = i2c[I2C_DEVICE_0];
    (void)adapter->clr_tx_abrt;
    while(position < length)
    {
        if(binding_request_cancelled(control, ticket, run_id))
            return -3;
        if(adapter->tx_abrt_source)
            return -1;
        if(adapter->status & I2C_STATUS_TFNF)
        {
            adapter->data_cmd = I2C_DATA_CMD_DATA(data[position++]);
            continue;
        }
        if(binding_i2c_timed_out(deadline))
            return -2;
    }
    while((adapter->status & I2C_STATUS_ACTIVITY) ||
          !(adapter->status & I2C_STATUS_TFE))
    {
        if(binding_request_cancelled(control, ticket, run_id))
            return -3;
        if(adapter->tx_abrt_source)
            return -1;
        if(binding_i2c_timed_out(deadline))
            return -2;
    }
    return adapter->tx_abrt_source ? -1 : 0;
}

static int binding_i2c_read(micropython_binding_control_t *control,
                            uint32_t ticket, uint32_t run_id,
                            uint8_t address, const uint8_t *prefix,
                            size_t prefix_length, uint8_t *data,
                            size_t length)
{
    volatile i2c_t *adapter;
    uint64_t deadline = hal_time_us() + MICROPYTHON_BINDING_I2C_TIMEOUT_US;
    size_t prefix_position = 0U;
    size_t commands = 0U;
    size_t received = 0U;

    if(binding_request_cancelled(control, ticket, run_id))
        return -3;
    binding_select_i2c(address);
    adapter = i2c[I2C_DEVICE_0];
    (void)adapter->clr_tx_abrt;
    while(prefix_position < prefix_length)
    {
        if(binding_request_cancelled(control, ticket, run_id))
            return -3;
        if(adapter->tx_abrt_source)
            return -1;
        if(adapter->status & I2C_STATUS_TFNF)
        {
            adapter->data_cmd = I2C_DATA_CMD_DATA(prefix[prefix_position++]);
            continue;
        }
        if(binding_i2c_timed_out(deadline))
            return -2;
    }
    while(received < length)
    {
        if(binding_request_cancelled(control, ticket, run_id))
            return -3;
        while(commands < length && adapter->txflr < 8U)
        {
            adapter->data_cmd = I2C_DATA_CMD_CMD;
            commands++;
        }
        while(received < length && adapter->rxflr)
            data[received++] = (uint8_t)adapter->data_cmd;
        if(adapter->tx_abrt_source)
            return -1;
        if(binding_i2c_timed_out(deadline))
            return -2;
    }
    while((adapter->status & I2C_STATUS_ACTIVITY) ||
          !(adapter->status & I2C_STATUS_TFE))
    {
        if(binding_request_cancelled(control, ticket, run_id))
            return -3;
        if(adapter->tx_abrt_source)
            return -1;
        if(binding_i2c_timed_out(deadline))
            return -2;
    }
    return adapter->tx_abrt_source ? -1 : 0;
}

static micropython_binding_result_t binding_execute(
    micropython_binding_control_t *control,
    uint32_t ticket, uint32_t run_id)
{
    uint32_t op = control->operation;
    uint32_t a[6];
    uint32_t input_length = control->input_length;

    for(uint8_t i = 0U; i < 6U; i++)
        a[i] = control->arguments[i];
    control->output_length = 0U;
    if(input_length > MICROPYTHON_BINDING_DATA_MAX)
        return MICROPYTHON_BINDING_ERROR_LIMIT;
    if(binding_request_cancelled(control, ticket, run_id))
        return MICROPYTHON_BINDING_ERROR_TIMEOUT;

    switch(op)
    {
    case MICROPYTHON_BINDING_OP_BUTTONS:
    {
#if defined(MICROPYTHON_BINDING_TESTING)
        uint32_t buttons = hk_input_state();
#else
        uint32_t buttons = 0U;
        if(binding_input_state(&buttons) != HK_OK)
            return MICROPYTHON_BINDING_ERROR_IO;
#endif
        control->data[0] = (uint8_t)buttons;
        control->data[1] = (uint8_t)(buttons >> 8);
        control->data[2] = (uint8_t)(buttons >> 16);
        control->data[3] = (uint8_t)(buttons >> 24);
        control->output_length = 4U;
        return MICROPYTHON_BINDING_OK;
    }
    case MICROPYTHON_BINDING_OP_DISPLAY_CLEAR:
    {
        lcd_overlay_command_t command = {0};

        if(!g_display_owned || g_display_stage.run_id != run_id ||
           a[0] > 0xFFFFU)
            return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
        binding_display_transaction_begin(ticket, run_id, 1U);
        /* clear() starts a fresh staged frame.  The currently presented
         * overlay remains visible until an explicit successful present(). */
        g_display_stage.command_count = 0U;
        g_display_stage.text_length = 0U;
        g_display_stage.overflowed = 0U;
        command.type = LCD_OVERLAY_COMMAND_CLEAR;
        command.color = (uint16_t)a[0];
        return binding_display_append(&command, NULL, 0U);
    }
    case MICROPYTHON_BINDING_OP_DISPLAY_TEXT:
    {
        lcd_overlay_command_t command = {0};

        if(!g_display_owned || g_display_stage.run_id != run_id)
            return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
        if(a[0] >= LCD_W || a[1] >= LCD_H ||
           a[2] > 0xFFFFU || a[3] > 0xFFFFU)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        binding_display_transaction_begin(ticket, run_id, 0U);
        command.type = LCD_OVERLAY_COMMAND_TEXT;
        command.x = (uint16_t)a[0];
        command.y = (uint16_t)a[1];
        command.color = (uint16_t)a[2];
        command.background = (uint16_t)a[3];
        return binding_display_append(
            &command, (const uint8_t *)control->data, input_length);
    }
    case MICROPYTHON_BINDING_OP_DISPLAY_RECT:
    {
        lcd_overlay_command_t command = {0};

        if(!g_display_owned || g_display_stage.run_id != run_id)
            return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
        if(a[0] >= LCD_W || a[1] >= LCD_H || !a[2] || !a[3] ||
           a[2] > 0xFFFFU || a[3] > 0xFFFFU || a[4] > 0xFFFFU)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        binding_display_transaction_begin(ticket, run_id, 0U);
        command.type = LCD_OVERLAY_COMMAND_RECT;
        command.filled = a[5] != 0U;
        command.x = (uint16_t)a[0];
        command.y = (uint16_t)a[1];
        command.width = (uint16_t)a[2];
        command.height = (uint16_t)a[3];
        command.color = (uint16_t)a[4];
        return binding_display_append(&command, NULL, 0U);
    }
    case MICROPYTHON_BINDING_OP_DISPLAY_PRESENT:
    {
        binding_display_cancel_context_t cancel_context = {
            control, ticket, run_id,
        };
        lcd_overlay_present_result_t display_result;

        if(!g_display_owned || g_display_stage.run_id != run_id)
            return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
        if(g_display_stage.overflowed)
            return MICROPYTHON_BINDING_ERROR_LIMIT;
        display_result = lcd_overlay_present(
            run_id, g_display_stage.commands,
            g_display_stage.command_count, g_display_stage.text,
            g_display_stage.text_length, binding_display_cancelled,
            &cancel_context);
        if(display_result == LCD_OVERLAY_PRESENT_OK)
            return MICROPYTHON_BINDING_OK;
        if(display_result == LCD_OVERLAY_PRESENT_CANCELLED)
            return MICROPYTHON_BINDING_ERROR_TIMEOUT;
        if(display_result == LCD_OVERLAY_PRESENT_INVALID)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        return MICROPYTHON_BINDING_ERROR_IO;
    }
    case MICROPYTHON_BINDING_OP_LED:
    {
        binding_display_cancel_context_t cancel_context = {
            control, ticket, run_id,
        };
        hk_cancel_t cancel = {
            binding_lights_cancelled, &cancel_context,
        };
        hk_result_t result;
        uint8_t already_owned =
            (g_lights_owned & HK_LIGHTS_CHANNEL_ILLUMINATION) != 0U;

        if(a[0] > 100U)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        result = binding_claim_light(
            "consumer:micropython-adapter",
            HK_LIGHTS_CHANNEL_ILLUMINATION,
            HK_LIGHTS_FEATURE_ILLUMINATION,
            &g_illumination_lights, &g_illumination_owner);
        if(result == HK_OK)
            result = hk_lights_set_level(
                g_illumination_owner, &g_illumination_lights,
                HK_LIGHTS_CHANNEL_ILLUMINATION, (uint16_t)a[0] * 10U,
                HK_DEADLINE_IMMEDIATE, &cancel);
        if(result != HK_OK && !already_owned &&
           !hk_lease_is_zero(&g_illumination_lights.lease))
        {
            (void)hk_lights_release(
                g_illumination_owner, HK_DEADLINE_IMMEDIATE,
                &g_illumination_lights);
            g_lights_owned &= ~HK_LIGHTS_CHANNEL_ILLUMINATION;
            settings_lights_restore(HK_LIGHTS_CHANNEL_ILLUMINATION);
        }
        return binding_lights_result(result);
    }
    case MICROPYTHON_BINDING_OP_RGB:
    {
        binding_display_cancel_context_t cancel_context = {
            control, ticket, run_id,
        };
        hk_cancel_t cancel = {
            binding_lights_cancelled, &cancel_context,
        };
        hk_result_t result;
        uint8_t already_owned =
            (g_lights_owned & HK_LIGHTS_CHANNEL_RGB) != 0U;

        if(a[0] > 255U || a[1] > 255U || a[2] > 255U)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        result = binding_claim_light(
            "consumer:micropython-adapter", HK_LIGHTS_CHANNEL_RGB,
            HK_LIGHTS_FEATURE_RGB, &g_rgb_lights, &g_rgb_owner);
        if(result == HK_OK)
            result = hk_lights_set_rgb(
                g_rgb_owner, &g_rgb_lights,
                binding_rgb_level(a[0]), binding_rgb_level(a[1]),
                binding_rgb_level(a[2]), HK_DEADLINE_IMMEDIATE, &cancel);
        if(result != HK_OK && !already_owned &&
           !hk_lease_is_zero(&g_rgb_lights.lease))
        {
            (void)hk_lights_release(
                g_rgb_owner, HK_DEADLINE_IMMEDIATE, &g_rgb_lights);
            g_lights_owned &= ~HK_LIGHTS_CHANNEL_RGB;
            settings_lights_restore(HK_LIGHTS_CHANNEL_RGB);
        }
        return binding_lights_result(result);
    }
    case MICROPYTHON_BINDING_OP_UART_INIT:
        if(a[0] < 1200U || a[0] > 2000000U)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        binding_select_uart(a[0]);
        return MICROPYTHON_BINDING_OK;
    case MICROPYTHON_BINDING_OP_UART_WRITE:
        /* UART writes are started and advanced incrementally by the service
         * tick so the core-0 main loop never waits for TX FIFO space. */
        return MICROPYTHON_BINDING_ERROR_BUSY;
    case MICROPYTHON_BINDING_OP_UART_READ:
    {
        size_t capacity = a[0];
        if(capacity > MICROPYTHON_BINDING_DATA_MAX)
            return MICROPYTHON_BINDING_ERROR_LIMIT;
        binding_select_uart(g_uart_baud);
        control->output_length = (uint32_t)hal_external_uart_receive(
            (uint8_t *)control->data, capacity);
        return MICROPYTHON_BINDING_OK;
    }
    case MICROPYTHON_BINDING_OP_I2C_WRITE:
    {
        int result;
        if(a[0] == 0U || a[0] > 0x7FU)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        result = binding_i2c_write(control, ticket, run_id, (uint8_t)a[0],
                                   (const uint8_t *)control->data,
                                   input_length);
        if(result != 0)
            binding_i2c_stop();
        return result == 0 ? MICROPYTHON_BINDING_OK :
               (result == -2 || result == -3) ?
                               MICROPYTHON_BINDING_ERROR_TIMEOUT :
                               MICROPYTHON_BINDING_ERROR_IO;
    }
    case MICROPYTHON_BINDING_OP_I2C_READ:
    {
        uint8_t output[MICROPYTHON_BINDING_DATA_MAX];
        int result;
        if(a[0] == 0U || a[0] > 0x7FU ||
           a[1] > MICROPYTHON_BINDING_DATA_MAX)
            return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
        result = binding_i2c_read(control, ticket, run_id, (uint8_t)a[0],
                                  (const uint8_t *)control->data,
                                  input_length, output, a[1]);
        if(result != 0)
        {
            binding_i2c_stop();
            return (result == -2 || result == -3) ?
                                  MICROPYTHON_BINDING_ERROR_TIMEOUT :
                                  MICROPYTHON_BINDING_ERROR_IO;
        }
        for(uint32_t i = 0U; i < a[1]; i++)
            control->data[i] = output[i];
        control->output_length = a[1];
        return MICROPYTHON_BINDING_OK;
    }
    default:
        return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
    }
}

static void binding_uart_write_reset(void)
{
    g_uart_write.ticket = 0U;
    g_uart_write.run_id = 0U;
    g_uart_write.length = 0U;
    g_uart_write.position = 0U;
    g_uart_write.active = 0U;
}

static void binding_uart_write_progress(
    micropython_binding_control_t *control)
{
    size_t written;
    uint32_t ticket = g_uart_write.ticket;

    if(!g_uart_write.active)
        return;
    if(binding_request_cancelled(control, ticket, g_uart_write.run_id))
    {
        binding_uart_write_reset();
        /* Reset the owned UART before acknowledging cancellation so bytes
         * already queued in hardware cannot leak out after caller unwind. */
        hal_external_uart_init(g_uart_baud);
        binding_complete_request(control, ticket,
                                 MICROPYTHON_BINDING_ERROR_TIMEOUT, 1U);
        return;
    }
    if(g_uart_write.position < g_uart_write.length)
    {
        written = hal_external_uart_send_ready(
            g_uart_write.data + g_uart_write.position,
            g_uart_write.length - g_uart_write.position);
        g_uart_write.position += written;
    }
    /* Queue completion is not transmission completion.  Keep the connector
     * lease until the hardware FIFO and shift register have both drained. */
    if(g_uart_write.position == g_uart_write.length &&
       hal_external_uart_tx_idle())
    {
        binding_uart_write_reset();
        binding_complete_request(control, ticket, MICROPYTHON_BINDING_OK, 0U);
    }
}

static void binding_uart_write_start(
    micropython_binding_control_t *control, uint32_t ticket, uint32_t run_id)
{
    binding_select_uart(g_uart_baud);
    g_uart_write.ticket = ticket;
    g_uart_write.run_id = run_id;
    g_uart_write.length = control->input_length;
    g_uart_write.position = 0U;
    for(size_t i = 0U; i < g_uart_write.length; i++)
        g_uart_write.data[i] = control->data[i];
    g_uart_write.active = 1U;
    binding_uart_write_progress(control);
}

void micropython_binding_service_prepare(uint32_t run_id)
{
    micropython_binding_control_t *control = binding_control();

    control->run_active = 0U;
    control->run_id = run_id;
    control->request_ticket = 0U;
    control->accepted_ticket = 0U;
    control->cancel_ticket = 0U;
    control->cancel_ack_ticket = 0U;
    control->complete_ticket = 0U;
    control->operation = 0U;
    control->input_length = 0U;
    control->output_length = 0U;
    control->result = MICROPYTHON_BINDING_OK;
    g_external_owned = 0U;
    g_lights_owned = 0U;
    g_external_mode = BINDING_EXTERNAL_NONE;
    g_uart_baud = 115200U;
    binding_uart_write_reset();
    binding_display_stage_reset(run_id);
    g_display_owned = lcd_overlay_acquire(run_id);
    __sync_synchronize();
    control->run_active = 1U;
}

void micropython_binding_service_tick(void)
{
    micropython_binding_control_t *control = binding_control();
    uint32_t ticket;
    uint32_t run_id;
    micropython_binding_result_t result;
    uint8_t cancelled;

    if(!control->run_active)
        return;
    if(g_uart_write.active)
    {
        binding_uart_write_progress(control);
        return;
    }
    ticket = control->request_ticket;
    if(ticket == control->complete_ticket)
        return;
    __sync_synchronize();
    run_id = control->run_id;
    if(binding_request_cancelled(control, ticket, run_id))
    {
        binding_complete_request(control, ticket,
                                 MICROPYTHON_BINDING_ERROR_TIMEOUT, 1U);
        return;
    }
    control->accepted_ticket = ticket;
    __sync_synchronize();
    if(control->input_length > MICROPYTHON_BINDING_DATA_MAX)
    {
        binding_complete_request(control, ticket,
                                 MICROPYTHON_BINDING_ERROR_LIMIT, 0U);
        return;
    }
    if(control->operation == MICROPYTHON_BINDING_OP_UART_WRITE)
    {
        binding_uart_write_start(control, ticket, run_id);
        return;
    }
    result = binding_execute(control, ticket, run_id);
    cancelled = binding_request_cancelled(control, ticket, run_id);
    binding_display_transaction_finish(ticket, run_id, cancelled);
    if(cancelled)
        result = MICROPYTHON_BINDING_ERROR_TIMEOUT;
    binding_complete_request(control, ticket, result, cancelled);
}

void micropython_binding_service_cleanup(void)
{
    micropython_binding_control_t *control = binding_control();
    uint32_t run_id = control->run_id;

    control->run_active = 0U;
    __sync_synchronize();
    if(control->request_ticket != control->complete_ticket)
    {
        uint32_t ticket = control->request_ticket;
        control->cancel_ticket = ticket;
        binding_uart_write_reset();
        binding_complete_request(control, ticket,
                                 MICROPYTHON_BINDING_ERROR_NOT_ACTIVE, 1U);
    }
    else
    {
        binding_uart_write_reset();
    }
    if(g_external_mode == BINDING_EXTERNAL_I2C)
        binding_i2c_stop();
    if(g_external_owned)
        external_link_service_resume();
    if(g_lights_owned & HK_LIGHTS_CHANNEL_ILLUMINATION)
        (void)hk_lights_release(
            g_illumination_owner, HK_DEADLINE_IMMEDIATE,
            &g_illumination_lights);
    if(g_lights_owned & HK_LIGHTS_CHANNEL_RGB)
        (void)hk_lights_release(
            g_rgb_owner, HK_DEADLINE_IMMEDIATE, &g_rgb_lights);
    settings_lights_restore(g_lights_owned);
    if(g_display_owned)
        (void)lcd_overlay_release(run_id);
    g_external_owned = 0U;
    g_lights_owned = 0U;
    g_display_owned = 0U;
    g_external_mode = BINDING_EXTERNAL_NONE;
    binding_display_stage_reset(0U);
}

micropython_binding_result_t micropython_binding_call(
    micropython_binding_op_t operation, const uint32_t arguments[6],
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_capacity, size_t *output_length)
{
    micropython_binding_control_t *control = binding_control();
    uint32_t ticket;
    uint64_t deadline;
    micropython_binding_result_t result;
    uint32_t produced;

    if(output_length)
        *output_length = 0U;
    if((input_length && !input) || input_length > MICROPYTHON_BINDING_DATA_MAX ||
       (output_capacity && !output) || !output_length)
        return MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT;
    if(!control->run_active)
        return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
    if(control->request_ticket != control->complete_ticket)
        return MICROPYTHON_BINDING_ERROR_BUSY;

    ticket = control->request_ticket + 1U;
    if(ticket == 0U)
        ticket = 1U;
    control->operation = (uint32_t)operation;
    for(uint8_t i = 0U; i < 6U; i++)
        control->arguments[i] = arguments ? arguments[i] : 0U;
    control->input_length = (uint32_t)input_length;
    control->output_length = 0U;
    control->result = MICROPYTHON_BINDING_ERROR_BUSY;
    control->accepted_ticket = 0U;
    control->cancel_ticket = 0U;
    control->cancel_ack_ticket = 0U;
    for(size_t i = 0U; i < input_length; i++)
        control->data[i] = input[i];
    __sync_synchronize();
    control->request_ticket = ticket;

    deadline = hal_time_us() + binding_rpc_timeout_us(operation, input_length);
    while(control->complete_ticket != ticket)
    {
        if(micropython_runtime_interrupt_pending())
        {
            binding_cancel_and_wait(control, ticket);
            /* Re-check and raise only after core 0 has acknowledged that it
             * will never touch this request context again. */
            micropython_runtime_vm_hook();
            return MICROPYTHON_BINDING_ERROR_NOT_ACTIVE;
        }
        if(hal_time_us() >= deadline)
        {
            binding_cancel_and_wait(control, ticket);
            return MICROPYTHON_BINDING_ERROR_TIMEOUT;
        }
        hal_sleep_ms(1U);
    }
    __sync_synchronize();
    result = (micropython_binding_result_t)control->result;
    produced = control->output_length;
    if(produced > output_capacity)
        return MICROPYTHON_BINDING_ERROR_LIMIT;
    for(uint32_t i = 0U; i < produced; i++)
        output[i] = control->data[i];
    *output_length = produced;
    return result;
}

const char *micropython_binding_result_name(
    micropython_binding_result_t result)
{
    switch(result)
    {
    case MICROPYTHON_BINDING_OK: return "ok";
    case MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT: return "invalid-argument";
    case MICROPYTHON_BINDING_ERROR_NOT_ACTIVE: return "not-active";
    case MICROPYTHON_BINDING_ERROR_BUSY: return "busy";
    case MICROPYTHON_BINDING_ERROR_TIMEOUT: return "timeout";
    case MICROPYTHON_BINDING_ERROR_IO: return "io";
    case MICROPYTHON_BINDING_ERROR_LIMIT: return "limit";
    default: return "unknown";
    }
}

#if defined(MICROPYTHON_BINDING_TESTING)
uint8_t micropython_binding_service_test_cancel_acknowledged(void)
{
    micropython_binding_control_t *control = binding_control();
    uint32_t ticket = control->cancel_ticket;

    return ticket != 0U && control->cancel_ack_ticket == ticket &&
           control->complete_ticket == ticket && !g_uart_write.active;
}

uint8_t micropython_binding_service_test_request_pending(void)
{
    micropython_binding_control_t *control = binding_control();

    return control->request_ticket != control->complete_ticket &&
           g_uart_write.active;
}
#endif
