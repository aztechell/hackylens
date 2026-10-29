#include "hal_power.h"
#include "hal_time.h"

#include <i2c.h>
#include <platform.h>

#define POWER_BUS_TIMEOUT_US 20000ULL

void hal_power_bus_init(void)
{
    i2c_init(I2C_DEVICE_1, 0x34U, 7U, 100000U);
}

uint8_t hal_power_read_register(uint8_t reg, uint8_t *value)
{
    volatile i2c_t *bus = (volatile i2c_t *)I2C1_BASE_ADDR;
    uint64_t deadline = hal_time_us() + POWER_BUS_TIMEOUT_US;
    uint8_t result;

    if(!value)
        return 0U;
    bus->enable = I2C_ENABLE_ENABLE;
    (void)bus->clr_tx_abrt;
    while(bus->rxflr)
    {
        if(hal_time_us() >= deadline)
            goto failed;
        (void)bus->data_cmd;
    }
    bus->data_cmd = reg;
    bus->data_cmd = I2C_DATA_CMD_CMD;
    while(bus->rxflr == 0U)
    {
        if(bus->tx_abrt_source || hal_time_us() >= deadline)
            goto failed;
    }
    result = (uint8_t)bus->data_cmd;
    while((bus->status & I2C_STATUS_ACTIVITY) ||
          !(bus->status & I2C_STATUS_TFE))
    {
        if(bus->tx_abrt_source || hal_time_us() >= deadline)
            goto failed;
    }
    if(bus->tx_abrt_source)
        goto failed;
    *value = result;
    return 1U;

failed:
    /* Bound a missing or stuck PMIC without resetting an unrelated bus. */
    bus->enable = 0U;
    (void)bus->clr_tx_abrt;
    return 0U;
}

uint8_t hal_power_write_register(uint8_t reg, uint8_t value)
{
    volatile i2c_t *bus = (volatile i2c_t *)I2C1_BASE_ADDR;
    uint64_t deadline = hal_time_us() + POWER_BUS_TIMEOUT_US;
    bus->enable = I2C_ENABLE_ENABLE;
    (void)bus->clr_tx_abrt;
    bus->data_cmd = reg;
    bus->data_cmd = value;
    while((bus->status & I2C_STATUS_ACTIVITY) ||
          !(bus->status & I2C_STATUS_TFE))
    {
        if(bus->tx_abrt_source || hal_time_us() >= deadline)
            goto failed;
    }
    if(bus->tx_abrt_source)
        goto failed;
    return 1U;
failed:
    bus->enable = 0U;
    (void)bus->clr_tx_abrt;
    return 0U;
}
