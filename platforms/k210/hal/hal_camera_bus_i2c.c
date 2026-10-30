#include "hal_camera_bus.h"
#include "hal_time.h"
#include "board_config.h"
#include <i2c.h>
#ifndef CAMERA_BUS_READ
#define CAMERA_BUS_READ(reg) (i2c[CAMERA_CONTROL_CONTROLLER]->reg)
#define CAMERA_BUS_WRITE(reg, value) (i2c[CAMERA_CONTROL_CONTROLLER]->reg = (value))
#endif
/* DesignWare DATA_CMD bit 9 explicitly terminates each phase with STOP. */
#define CAMERA_I2C_STOP (1U << 9)
static uint8_t ready;
static hk_result_t fail(hk_result_t result)
{
    CAMERA_BUS_WRITE(enable, 0U);
    ready = 0;
    return result;
}
static hk_result_t check(uint64_t deadline)
{
    if(CAMERA_BUS_READ(tx_abrt_source)) return fail(HK_ERR_IO);
    if(hal_time_us() >= deadline) return fail(HK_ERR_DEADLINE_EXCEEDED);
    return HK_OK;
}
hk_result_t hal_camera_bus_init(hal_camera_bus_info_t *info, uint64_t deadline)
{
    ready = 0;
    if(hal_time_us() >= deadline) return HK_ERR_DEADLINE_EXCEEDED;
    i2c_init((i2c_device_number_t)CAMERA_CONTROL_CONTROLLER, CAMERA_CONTROL_ADDRESS_7BIT, 7U, CAMERA_CONTROL_CLOCK_HZ);
    CAMERA_BUS_WRITE(enable, 0U);
    (void)CAMERA_BUS_READ(clr_tx_abrt);
    while(CAMERA_BUS_READ(rxflr)) {
        if(hal_time_us() >= deadline) return fail(HK_ERR_DEADLINE_EXCEEDED);
        (void)CAMERA_BUS_READ(data_cmd);
    }
    CAMERA_BUS_WRITE(enable, I2C_ENABLE_ENABLE);
    hk_result_t result = check(deadline);
    if(result != HK_OK) return result;
    ready = 1;
    if(info) *info = (hal_camera_bus_info_t){CAMERA_CONTROL_CLOCK_HZ, CAMERA_CONTROL_ADDRESS_7BIT, 0, 0};
    return HK_OK;
}
static hk_result_t phase(const uint8_t *data, unsigned count, uint8_t *received, uint64_t deadline)
{
    hk_result_t result;
    (void)CAMERA_BUS_READ(clr_stop_det);
    unsigned commands = count + (received ? 1U : 0U);
    for(unsigned i = 0; i < commands; ++i) {
        do { result = check(deadline); if(result != HK_OK) return result; }
        while(!(CAMERA_BUS_READ(status) & I2C_STATUS_TFNF));
        uint32_t cmd = i < count ? data[i] : I2C_DATA_CMD_CMD;
        if(i + 1U == commands) cmd |= CAMERA_I2C_STOP;
        CAMERA_BUS_WRITE(data_cmd, cmd);
    }
    for(;;) {
        result = check(deadline);
        if(result != HK_OK) return result;
        if((CAMERA_BUS_READ(raw_intr_stat) & I2C_INTR_STAT_STOP_DET) &&
           !(CAMERA_BUS_READ(status) & I2C_STATUS_ACTIVITY) &&
           (CAMERA_BUS_READ(status) & I2C_STATUS_TFE) &&
           (!received || CAMERA_BUS_READ(rxflr))) break;
    }
    if(received) *received = (uint8_t)CAMERA_BUS_READ(data_cmd);
    return HK_OK;
}
hk_result_t hal_camera_bus_write8(uint8_t reg, uint8_t value, uint64_t deadline)
{
    if(!ready) return HK_ERR_INVALID_STATE;
    const uint8_t data[] = {reg, value};
    return phase(data, 2, 0, deadline);
}
hk_result_t hal_camera_bus_read8(uint8_t reg, uint8_t *out, uint64_t deadline)
{
    if(!out) return HK_ERR_INVALID_ARGUMENT;
    if(!ready) return HK_ERR_INVALID_STATE;
    hk_result_t result = phase(&reg, 1, 0, deadline);
    if(result != HK_OK) return result;
    uint8_t value;
    result = phase(0, 0, &value, deadline);
    if(result == HK_OK) *out = value;
    return result;
}
