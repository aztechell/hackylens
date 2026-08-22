#include "hal_external_link.h"

#if defined(HAL_EXTERNAL_LINK_TESTING)
#include "hal_external_link_test_platform.h"
#else
#include <i2c.h>
#include <platform.h>
#include <plic.h>
#include <uart.h>

#include "../../../firmware/src/internal/hk_board_port.h"
#endif

#if defined(HAL_EXTERNAL_LINK_TESTING)
#define PREPARE_EXTERNAL_UART() board_external_link_uart_pins()
#define PREPARE_EXTERNAL_I2C() board_external_link_i2c_pins()
#else
#define PREPARE_EXTERNAL_UART() hk_board_ops.external_uart_prepare()
#define PREPARE_EXTERNAL_I2C() hk_board_ops.external_i2c_prepare()
#endif

/* K210 general-purpose UART1/2/3 expose an 8-byte TX FIFO. */
#define HAL_EXTERNAL_UART_FIFO_DEPTH 8U
#define HAL_EXTERNAL_UART_LSR_TEMT (1U << 6)
#define HAL_EXTERNAL_UART_RX_IRQ_PRIORITY 2U
#if defined(HAL_EXTERNAL_LINK_TESTING)
#define HAL_EXTERNAL_UART_ADAPTER (uart[UART_DEVICE_1])
#else
#define HAL_EXTERNAL_UART_ADAPTER ((volatile uart_t *)UART1_BASE_ADDR)
#endif

static const hal_external_i2c_callbacks_t *g_i2c_callbacks;
static uint8_t g_i2c_active;
static uint8_t g_i2c_skip_sdk_receive;
static hal_external_uart_receive_fn g_uart_receive;
static void *g_uart_receive_context;
static uint8_t g_uart_active;

static int uart_receive_irq(void *context)
{
    uint8_t bytes[HAL_EXTERNAL_UART_FIFO_DEPTH];
    size_t count;

    (void)context;
    do
    {
        count = (size_t)uart_receive_data(
            UART_DEVICE_1, (char *)bytes, sizeof(bytes));
        if(g_uart_receive)
        {
            for(size_t index = 0U; index < count; ++index)
                g_uart_receive(g_uart_receive_context, bytes[index]);
        }
    } while(count != 0U);
    return 0;
}

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

void hal_external_uart_stop(void)
{
    if(!g_uart_active)
        return;
    uart_irq_unregister(UART_DEVICE_1, UART_RECEIVE);
    g_uart_active = 0U;
    g_uart_receive = NULL;
    g_uart_receive_context = NULL;
    uart_init(UART_DEVICE_1);
}

void hal_external_uart_init(uint32_t baud,
                            hal_external_uart_receive_fn receive,
                            void *context)
{
    hal_external_i2c_stop();
    hal_external_uart_stop();
    PREPARE_EXTERNAL_UART();
    uart_init(UART_DEVICE_1);
    uart_configure(UART_DEVICE_1, baud, UART_BITWIDTH_8BIT, UART_STOP_1, UART_PARITY_NONE);
    (void)uart_receive_irq(NULL);
    g_uart_receive = receive;
    g_uart_receive_context = context;
    uart_irq_register(UART_DEVICE_1, UART_RECEIVE, uart_receive_irq, NULL,
                      HAL_EXTERNAL_UART_RX_IRQ_PRIORITY);
    g_uart_active = 1U;
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

size_t hal_external_uart_send_ready(const uint8_t *data, size_t len)
{
    volatile uart_t *adapter = HAL_EXTERNAL_UART_ADAPTER;
    uint32_t level;
    size_t capacity;

    if(!data || !len)
        return 0U;
    level = adapter->TFL;
    if(level >= HAL_EXTERNAL_UART_FIFO_DEPTH)
        return 0U;
    capacity = HAL_EXTERNAL_UART_FIFO_DEPTH - level;
    if(capacity > len)
        capacity = len;
    for(size_t i = 0U; i < capacity; i++)
        adapter->THR = data[i];
    return capacity;
}

uint8_t hal_external_uart_tx_idle(void)
{
    volatile uart_t *adapter = HAL_EXTERNAL_UART_ADAPTER;

    return adapter->TFL == 0U &&
           (adapter->LSR & HAL_EXTERNAL_UART_LSR_TEMT) != 0U;
}

void hal_external_i2c_init(uint8_t address, const hal_external_i2c_callbacks_t *callbacks)
{
    hal_external_i2c_stop();
    g_i2c_callbacks = callbacks;
    g_i2c_skip_sdk_receive = 0U;
    PREPARE_EXTERNAL_I2C();
    i2c_init_as_slave(I2C_DEVICE_0, address, 7U, &g_i2c_handler);
    g_i2c_active = 1U;
}

void hal_external_i2c_controller_init(uint8_t address, uint32_t frequency_hz)
{
    hal_external_i2c_stop();
    PREPARE_EXTERNAL_I2C();
    i2c_init(I2C_DEVICE_0, address, 7U, frequency_hz);
    (void)i2c[I2C_DEVICE_0]->clr_tx_abrt;
    g_i2c_active = 1U;
}

uint8_t hal_external_i2c_controller_aborted(void)
{
    return (uint8_t)(i2c[I2C_DEVICE_0]->tx_abrt_source != 0U);
}

uint8_t hal_external_i2c_controller_tx_ready(void)
{
    return (uint8_t)((i2c[I2C_DEVICE_0]->status & I2C_STATUS_TFNF) != 0U);
}

uint8_t hal_external_i2c_controller_rx_ready(void)
{
    return (uint8_t)(i2c[I2C_DEVICE_0]->rxflr != 0U);
}

uint8_t hal_external_i2c_controller_idle(void)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];

    return (uint8_t)((adapter->status & I2C_STATUS_ACTIVITY) == 0U &&
                     (adapter->status & I2C_STATUS_TFE) != 0U);
}

void hal_external_i2c_controller_write(uint8_t byte)
{
    i2c[I2C_DEVICE_0]->data_cmd = I2C_DATA_CMD_DATA(byte);
}

void hal_external_i2c_controller_request_read(void)
{
    i2c[I2C_DEVICE_0]->data_cmd = I2C_DATA_CMD_CMD;
}

uint8_t hal_external_i2c_controller_read(void)
{
    return (uint8_t)i2c[I2C_DEVICE_0]->data_cmd;
}

uint32_t hal_external_i2c_target_lock(void)
{
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    return g_i2c_active;
}

void hal_external_i2c_target_unlock(uint32_t state)
{
    if(state)
        plic_irq_enable(IRQN_I2C0_INTERRUPT);
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
