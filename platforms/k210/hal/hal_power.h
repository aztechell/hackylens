#ifndef HK_HAL_POWER_H
#define HK_HAL_POWER_H

#include <stdint.h>

/* Private AXP173 management bus, independent of the external I2C0 service. */
void hal_power_bus_init(void);
uint8_t hal_power_read_register(uint8_t reg, uint8_t *value);
uint8_t hal_power_write_register(uint8_t reg, uint8_t value);

typedef enum {
    HAL_POWER_INIT_DISABLE = 1,
    HAL_POWER_INIT_ENABLE,
    HAL_POWER_RX_DRAIN,
    HAL_POWER_RX_WAIT,
    HAL_POWER_RX_IDLE,
    HAL_POWER_TX_IDLE,
    HAL_POWER_BUS_CLEAR,
    HAL_POWER_GPIO_COLLISION,
} hal_power_failure_stage_t;
/* Last failure, sampled before disabling/clearing the controller. */
typedef struct {
    uint32_t failures, abort_source, status, raw_intr, txflr, rxflr;
    uint32_t enable, enable_status, con, tar, hcnt, lcnt;
    uint8_t stage, reg, scl_pad, sda_pad, scl_function, sda_function;
} hal_power_diagnostic_t;
void hal_power_get_diagnostic(hal_power_diagnostic_t *diagnostic);

#endif
