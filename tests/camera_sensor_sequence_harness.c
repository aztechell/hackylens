#include <assert.h>
#include <stdio.h>
#include "board_config.h"
#include "camera_sensor.h"
#include "hk_board_port.h"
#include "hal_dvp.h"
#include "hal_time.h"

static uint64_t now;
static unsigned prepares, dvp_inits, writes, reads, fail_write, fail_read;
static void prepare(void) { prepares++; }
const hk_board_ops_t hk_board_ops = {.camera_prepare = prepare};
uint64_t hal_time_us(void) { return now; }
void hal_sleep_ms(uint32_t ms) { now += (uint64_t)ms * 1000U; }
void hal_dvp_init_bus(uint8_t bits) { assert(bits == 8); dvp_inits++; }
uint32_t hal_dvp_set_xclk_rate(uint32_t hz) { return hz; }
hk_result_t hal_camera_bus_init(hal_camera_bus_info_t *info, uint64_t deadline)
{
    assert(deadline == now + 20000U);
#if HK_BOARD_CAMERA_OV7740
    *info = (hal_camera_bus_info_t){100000, 0x21, 0, 0};
#else
    *info = (hal_camera_bus_info_t){0, 0x30, 255, 255};
#endif
    return HK_OK;
}
hk_result_t hal_camera_bus_write8(uint8_t reg, uint8_t value, uint64_t deadline)
{
    (void)reg; (void)value;
    assert(deadline == now + 20000U);
    writes++;
    return fail_write && writes == fail_write ? HK_ERR_IO : HK_OK;
}
hk_result_t hal_camera_bus_read8(uint8_t reg, uint8_t *out, uint64_t deadline)
{
    assert(out && deadline == now + 20000U);
    reads++;
    if(fail_read && reads == fail_read) return HK_ERR_DEADLINE_EXCEEDED;
    if(reg == 0x1C) *out = 0x7F;
    else if(reg == 0x1D) *out = 0xA2;
    else if(reg == 0x0A) *out = HK_BOARD_CAMERA_OV7740 ? 0x77 : 0x26;
    else if(reg == 0x0B) *out = 0x42;
    else *out = 0xFF;
    return HK_OK;
}
int main(void)
{
    uint32_t xclk, rate;
    uint16_t mid = 0xDEAD, pid = 0xBEEF;
    assert(camera_sensor_init_bus(&xclk, &rate) == HK_OK);
    assert(prepares == 1 && dvp_inits == 1 && xclk == CAMERA_XCLK_HZ);
    assert(rate == (HK_BOARD_CAMERA_OV7740 ? 100000U : 0U));
    assert(now == (HK_BOARD_CAMERA_OV7740 ? 20000U : 0U));
    assert(camera_sensor_input_format() ==
           (HK_BOARD_CAMERA_OV7740 ? CAMERA_INPUT_YUV422 : CAMERA_INPUT_RGB565));

    fail_read = 2;
    assert(camera_sensor_probe_id(&mid, &pid) == HK_ERR_DEADLINE_EXCEEDED);
    assert(mid == 0xDEAD && pid == 0xBEEF && reads == 2);
    fail_read = reads = 0;
    assert(camera_sensor_probe_id(&mid, &pid) == HK_OK);
    assert(camera_sensor_matches_id(mid, pid));

    for(unsigned failure = 1; failure <= 5; failure++) {
        writes = 0; fail_write = failure;
        assert(camera_sensor_apply_init_table() == HK_ERR_IO);
        assert(writes == failure); /* No writes after the first failed register. */
    }
    fail_write = writes = 0;
    assert(camera_sensor_apply_init_table() == HK_OK && writes > 50);
    writes = 0;
    assert(camera_sensor_select_bank(1) == HK_OK);
    assert(writes == (HK_BOARD_CAMERA_OV7740 ? 0U : 1U));
    puts("CAMERA_SENSOR_SEQUENCE_OK");
    return 0;
}
