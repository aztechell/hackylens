#ifndef HK_HAL_POWER_H
#define HK_HAL_POWER_H

#include <stdint.h>

/* Private AXP173 management bus, independent of the external I2C0 service. */
void hal_power_bus_init(void);
uint8_t hal_power_read_register(uint8_t reg, uint8_t *value);
uint8_t hal_power_write_register(uint8_t reg, uint8_t value);

#endif
