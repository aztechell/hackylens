#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dvp.h>
static dvp_t mmio;
static uint64_t now;
static unsigned latency = 2, ticks, transfers;
static uint8_t stuck, byte;
static uint32_t controls[8];
uint64_t hal_time_us(void) { return now++; }
static uint32_t read_mmio(uint32_t *reg)
{
    if(reg == &mmio.sts && (mmio.sts & DVP_STS_SCCB_EN) && !stuck && ++ticks >= latency)
    { mmio.sts = 0; mmio.sccb_cfg = (mmio.sccb_cfg & 0xFFFFFFU) | ((uint32_t)byte << 24); }
    return *reg;
}
static void write_mmio(uint32_t *reg, uint32_t value)
{
    *reg = value;
    if(reg == &mmio.sts) { ticks = 0; controls[transfers++ % 8] = mmio.sccb_ctl; }
}
#define CAMERA_BUS_READ(reg) read_mmio(&mmio.reg)
#define CAMERA_BUS_WRITE(reg, value) write_mmio(&mmio.reg, value)
#include "../platforms/k210/hal/hal_camera_bus_dvp.c"
int main(void)
{
    hal_camera_bus_info_t info;
    assert(hal_camera_bus_init(&info, now+100) == HK_OK);
    assert(info.address_7bit == 0x30 && info.configured_hz == 0);
    assert(info.scl_low_count == 255 && info.scl_high_count == 255);
    assert((mmio.sccb_cfg & 0xFFFF00) == 0xFFFF00);
    assert(hal_camera_bus_write8(0x12, 0x80, now+100) == HK_OK);
    assert(controls[0] == (0x61U | 0x1200U | 0x800000U));
    for(unsigned value = 0; value <= 255; value += 255) {
        uint8_t out = 0x5A; byte = (uint8_t)value; transfers = 0;
        assert(hal_camera_bus_read8(0x0A, &out, now+100) == HK_OK);
        assert(out == value && transfers == 2);
        assert(controls[0] == 0xA61 && controls[1] == 0x60);
    }
    uint8_t out = 0xA5;
    stuck = 1;
    assert(hal_camera_bus_read8(1, &out, now+5) == HK_ERR_DEADLINE_EXCEEDED);
    assert(out == 0xA5);
    assert(hal_camera_bus_write8(1, 2, now+100) == HK_ERR_INVALID_STATE);
    assert(hal_camera_bus_init(&info, now+5) == HK_ERR_DEADLINE_EXCEEDED);
    stuck = 0; mmio.sts = 0;
    assert(hal_camera_bus_init(&info, now+100) == HK_OK);
    transfers = 0; latency = 5;
    /* Both phases share this deadline: phase two cannot reset its budget. */
    uint64_t deadline = now + 8;
    assert(hal_camera_bus_read8(1, &out, deadline) == HK_ERR_DEADLINE_EXCEEDED);
    assert(out == 0xA5 && now <= deadline+2 && transfers == 2);
    assert(hal_camera_bus_read8(1, NULL, now+100) == HK_ERR_INVALID_ARGUMENT);
    puts("CAMERA_BUS_DVP_OK");
}
