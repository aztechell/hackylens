#include "hal_camera_bus.h"
#include "hal_time.h"
#include "board_config.h"
#include <dvp.h>
/* Hooks permit host MMIO emulation without replacing transport logic. */
#ifndef CAMERA_BUS_READ
#define CAMERA_BUS_READ(reg) (dvp->reg)
#define CAMERA_BUS_WRITE(reg, value) (dvp->reg = (value))
#endif
static uint8_t ready;
static hk_result_t wait_idle(uint64_t deadline)
{
    while(CAMERA_BUS_READ(sts) & DVP_STS_SCCB_EN)
        if(hal_time_us() >= deadline) { ready = 0; return HK_ERR_DEADLINE_EXCEEDED; }
    if(hal_time_us() >= deadline) { ready = 0; return HK_ERR_DEADLINE_EXCEEDED; }
    return HK_OK;
}
static hk_result_t transfer(uint64_t deadline)
{
    hk_result_t result = wait_idle(deadline);
    if(result != HK_OK) return result;
    CAMERA_BUS_WRITE(sts, DVP_STS_SCCB_EN | DVP_STS_SCCB_EN_WE);
    return wait_idle(deadline);
}
hk_result_t hal_camera_bus_init(hal_camera_bus_info_t *info, uint64_t deadline)
{
    ready = 0;
    hk_result_t result = wait_idle(deadline);
    if(result != HK_OK) return result;
    uint32_t cfg = CAMERA_BUS_READ(sccb_cfg) & ~(DVP_SCCB_SCL_LCNT_MASK | DVP_SCCB_SCL_HCNT_MASK);
    CAMERA_BUS_WRITE(sccb_cfg, cfg | DVP_SCCB_SCL_LCNT(CAMERA_CONTROL_SCL_LOW_COUNT) | DVP_SCCB_SCL_HCNT(CAMERA_CONTROL_SCL_HIGH_COUNT));
    ready = 1;
    if(info) *info = (hal_camera_bus_info_t){0, CAMERA_CONTROL_ADDRESS_7BIT, CAMERA_CONTROL_SCL_LOW_COUNT, CAMERA_CONTROL_SCL_HIGH_COUNT};
    return HK_OK;
}
hk_result_t hal_camera_bus_write8(uint8_t reg, uint8_t value, uint64_t deadline)
{
    if(!ready) return HK_ERR_INVALID_STATE;
    hk_result_t result = wait_idle(deadline);
    if(result != HK_OK) return result;
    CAMERA_BUS_WRITE(sccb_cfg, (CAMERA_BUS_READ(sccb_cfg) & ~DVP_SCCB_BYTE_NUM_MASK) | DVP_SCCB_BYTE_NUM_3);
    CAMERA_BUS_WRITE(sccb_ctl, DVP_SCCB_WRITE_DATA_ENABLE | DVP_SCCB_DEVICE_ADDRESS(CAMERA_CONTROL_ADDRESS_7BIT << 1) | DVP_SCCB_REG_ADDRESS(reg) | DVP_SCCB_WDATA_BYTE0(value));
    return transfer(deadline);
}
hk_result_t hal_camera_bus_read8(uint8_t reg, uint8_t *out, uint64_t deadline)
{
    if(!out) return HK_ERR_INVALID_ARGUMENT;
    if(!ready) return HK_ERR_INVALID_STATE;
    hk_result_t result = wait_idle(deadline);
    if(result != HK_OK) return result;
    CAMERA_BUS_WRITE(sccb_cfg, (CAMERA_BUS_READ(sccb_cfg) & ~DVP_SCCB_BYTE_NUM_MASK) | DVP_SCCB_BYTE_NUM_2);
    CAMERA_BUS_WRITE(sccb_ctl, DVP_SCCB_WRITE_DATA_ENABLE | DVP_SCCB_DEVICE_ADDRESS(CAMERA_CONTROL_ADDRESS_7BIT << 1) | DVP_SCCB_REG_ADDRESS(reg));
    result = transfer(deadline);
    if(result != HK_OK) return result;
    CAMERA_BUS_WRITE(sccb_ctl, DVP_SCCB_DEVICE_ADDRESS(CAMERA_CONTROL_ADDRESS_7BIT << 1));
    result = transfer(deadline);
    if(result == HK_OK) *out = (uint8_t)DVP_SCCB_RDATA_BYTE(CAMERA_BUS_READ(sccb_cfg));
    return result;
}
