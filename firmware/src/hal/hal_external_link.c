#include "hal_external_link.h"

#include <i2c.h>
#include <platform.h>
#include <plic.h>
#include <uart.h>

#include "../board/board_hackylens.h"

static const hal_external_i2c_callbacks_t *g_i2c_callbacks;
static uint8_t g_i2c_active;
static uint8_t g_i2c_skip_sdk_receive;

static void i2c_receive(uint32_t data)
{
    if(g_i2c_skip_sdk_receive)
    {
        g_i2c_skip_sdk_receive = 0U;
        return;
    }
    if(g_i2c_callbacks && g_i2c_callbacks->receive)
        g_i2c_callbacks->receive((uint8_t)data);
}

static uint32_t i2c_transmit(void)
{
    if(g_i2c_callbacks && g_i2c_callbacks->transmit)
        return g_i2c_callbacks->transmit();
    return 0U;
}

static void i2c_event(i2c_event_t event)
{
    if(!g_i2c_callbacks || !g_i2c_callbacks->event)
        return;
    if(event == I2C_EV_STOP)
    {
        volatile i2c_t *adapter = i2c[I2C_DEVICE_0];
        uint8_t sdk_will_read = (adapter->intr_stat & I2C_INTR_STAT_RX_FULL) != 0U;

        /* The SDK announces STOP before servicing RX_FULL and consumes only
           one FIFO byte per IRQ. Drain the completed write here so the main
           loop can prepare the response before the master's read START. */
        while(adapter->rxflr & I2C_RXFLR_VALUE_MASK)
            g_i2c_callbacks->receive((uint8_t)adapter->data_cmd);
        g_i2c_skip_sdk_receive = sdk_will_read;
    }
    g_i2c_callbacks->event(event == I2C_EV_STOP ? HAL_EXTERNAL_I2C_EVENT_STOP :
                                                HAL_EXTERNAL_I2C_EVENT_START);
}

static const i2c_slave_handler_t g_i2c_handler = {
    .on_receive = i2c_receive,
    .on_transmit = i2c_transmit,
    .on_event = i2c_event,
};

void hal_external_uart_init(uint32_t baud)
{
    hal_external_i2c_stop();
    board_external_link_uart_pins();
    uart_init(UART_DEVICE_1);
    uart_configure(UART_DEVICE_1, baud, UART_BITWIDTH_8BIT, UART_STOP_1, UART_PARITY_NONE);
}

size_t hal_external_uart_receive(uint8_t *data, size_t len)
{
    return (size_t)uart_receive_data(UART_DEVICE_1, (char *)data, len);
}

void hal_external_uart_send(const uint8_t *data, size_t len)
{
    if(data && len)
        uart_send_data(UART_DEVICE_1, (const char *)data, len);
}

void hal_external_i2c_init(uint8_t address, const hal_external_i2c_callbacks_t *callbacks)
{
    hal_external_i2c_stop();
    g_i2c_callbacks = callbacks;
    g_i2c_skip_sdk_receive = 0U;
    board_external_link_i2c_pins();
    i2c_init_as_slave(I2C_DEVICE_0, address, 7U, &g_i2c_handler);
    g_i2c_active = 1U;
}

void hal_external_i2c_stop(void)
{
    volatile i2c_t *adapter;

    if(!g_i2c_active)
        return;
    adapter = i2c[I2C_DEVICE_0];
    adapter->intr_mask = 0U;
    adapter->enable = 0U;
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    g_i2c_callbacks = NULL;
    g_i2c_active = 0U;
    g_i2c_skip_sdk_receive = 0U;
}
