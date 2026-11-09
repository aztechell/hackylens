#ifndef HK_HAL_ONBOARD_I2C_H
#define HK_HAL_ONBOARD_I2C_H

#include <stdint.h>

/* Cube's core-0 I2C1 bus, shared by the PMIC, codec and accelerometer. */
uint8_t hal_onboard_i2c_read(uint8_t address, uint8_t reg, uint8_t *value);
uint8_t hal_onboard_i2c_read_bytes(uint8_t address, uint8_t reg, uint8_t *values,
                                  uint8_t count);
uint8_t hal_onboard_i2c_write(uint8_t address, uint8_t reg, uint8_t value);

#endif
