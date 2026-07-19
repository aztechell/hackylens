#ifndef HK_HAL_EXTERNAL_LINK_H
#define HK_HAL_EXTERNAL_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    HAL_EXTERNAL_I2C_EVENT_START = 0,
    HAL_EXTERNAL_I2C_EVENT_STOP,
} hal_external_i2c_event_t;

typedef struct
{
    void (*receive)(uint8_t byte);
    uint8_t (*transmit)(void);
    void (*event)(hal_external_i2c_event_t event);
} hal_external_i2c_callbacks_t;

void hal_external_uart_init(uint32_t baud);
size_t hal_external_uart_receive(uint8_t *data, size_t len);
void hal_external_uart_send(const uint8_t *data, size_t len);
void hal_external_i2c_init(uint8_t address, const hal_external_i2c_callbacks_t *callbacks);
void hal_external_i2c_stop(void);

#endif
