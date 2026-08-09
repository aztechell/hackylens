#ifndef HK_MICROPYTHON_BINDING_TEST_PLATFORM_H
#define HK_MICROPYTHON_BINDING_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "hk_lcd.h"

#define LCD_W 320U
#define LCD_H 240U

#define I2C_DEVICE_0 0
#define IRQN_I2C0_INTERRUPT 1
#define I2C_STATUS_TFNF 0x01U
#define I2C_STATUS_ACTIVITY 0x02U
#define I2C_STATUS_TFE 0x04U
#define I2C_DATA_CMD_DATA(value) ((uint32_t)(value))
#define I2C_DATA_CMD_CMD 0x100U

typedef struct
{
    volatile uint32_t intr_mask;
    volatile uint32_t enable;
    volatile uint32_t clr_tx_abrt;
    volatile uint32_t tx_abrt_source;
    volatile uint32_t status;
    volatile uint32_t data_cmd;
    volatile uint32_t txflr;
    volatile uint32_t rxflr;
} i2c_t;

extern volatile i2c_t *i2c[1];

void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency);
int plic_irq_disable(int interrupt);

void external_link_service_suspend(void);
void external_link_service_resume(void);
void illum_led_apply(void);
void rgb_led_apply(void);
void board_external_link_i2c_pins(void);
uint32_t hk_input_state(void);
void lights_illum_set(uint8_t enabled, uint8_t brightness);
void lights_rgb_set(uint8_t enabled, uint8_t red,
                    uint8_t green, uint8_t blue);
void hal_external_uart_init(uint32_t baud);
size_t hal_external_uart_receive(uint8_t *data, size_t length);
size_t hal_external_uart_send_ready(const uint8_t *data, size_t length);
uint8_t hal_external_uart_tx_idle(void);
uint64_t hal_time_us(void);
void hal_sleep_ms(uint32_t duration_ms);

uint8_t micropython_runtime_interrupt_pending(void);
void micropython_runtime_vm_hook(void);

#endif
