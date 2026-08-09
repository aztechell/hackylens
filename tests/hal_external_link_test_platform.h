#ifndef HK_HAL_EXTERNAL_LINK_TEST_PLATFORM_H
#define HK_HAL_EXTERNAL_LINK_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define UART_DEVICE_1 0
#define UART_BITWIDTH_8BIT 8
#define UART_STOP_1 1
#define UART_PARITY_NONE 0
#define I2C_DEVICE_0 0
#define IRQN_I2C0_INTERRUPT 1
#define I2C_INTR_STAT_RX_FULL 0x01U
#define I2C_RXFLR_VALUE_MASK 0xffU

typedef struct
{
    volatile uint32_t THR;
    volatile uint32_t TFL;
    volatile uint32_t LSR;
} uart_t;

typedef struct
{
    volatile uint32_t intr_stat;
    volatile uint32_t rxflr;
    volatile uint32_t data_cmd;
    volatile uint32_t intr_mask;
    volatile uint32_t enable;
} i2c_t;

typedef enum
{
    I2C_EV_START = 0,
    I2C_EV_STOP,
} i2c_event_t;

typedef struct
{
    void (*on_receive)(uint32_t data);
    uint32_t (*on_transmit)(void);
    void (*on_event)(i2c_event_t event);
} i2c_slave_handler_t;

extern volatile uart_t *uart[1];
extern volatile i2c_t *i2c[1];

void board_external_link_uart_pins(void);
void board_external_link_i2c_pins(void);
void uart_init(int device);
void uart_configure(int device, uint32_t baud, int width,
                    int stop, int parity);
int uart_receive_data(int device, char *data, size_t length);
int uart_send_data(int device, const char *data, size_t length);
void i2c_init_as_slave(int device, uint32_t address, uint32_t address_width,
                       const i2c_slave_handler_t *handler);
int plic_irq_disable(int interrupt);

#endif
