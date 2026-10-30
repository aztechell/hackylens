#include "camera_sensor.h"

#include "board_config.h"
#include "../internal/hk_board_port.h"
#include "../config/camera_config.h"
#include "hal_dvp.h"
#include "hal_time.h"
#include <stdio.h>

#if HK_BOARD_CAMERA_OV7740
#include "ov7740_sensor.h"
#define SENSOR_FN(name) ov7740_##name
#else
#include "ov2640_sensor.h"
#define SENSOR_FN(name) ov2640_##name
#endif

hk_result_t camera_sensor_init_bus(uint32_t *xclk, uint32_t *sccb)
{
    hal_camera_bus_info_t info;
    hk_board_ops.camera_prepare();
    hal_dvp_init_bus(8U);
    *xclk = hal_dvp_set_xclk_rate(CAMERA_XCLK_HZ);
    *sccb = 0;
    hk_result_t result = hal_camera_bus_init(&info, hal_time_us() + 20000U);
    if(result != HK_OK) return result;
    *sccb = info.configured_hz;
    printf("[CAMBUS] backend=%s addr7=0x%02X configured_hz=%u scl_low=%u scl_high=%u\r\n",
           CAMERA_CONTROL_BACKEND, info.address_7bit, info.configured_hz, info.scl_low_count, info.scl_high_count);
#if HK_BOARD_CAMERA_OV7740
    hal_sleep_ms(20U);
#endif
    return HK_OK;
}

hk_result_t camera_sensor_probe_id(uint16_t *mid, uint16_t *pid)
{
    return SENSOR_FN(probe_id)(mid, pid);
}

uint8_t camera_sensor_matches_id(uint16_t mid, uint16_t pid)
{
#if HK_BOARD_CAMERA_OV7740
    return (uint8_t)(mid == 0x7FA2U && pid == 0x7742U);
#else
    return (uint8_t)(mid == 0x7FA2U && pid == 0x2642U);
#endif
}

const char *camera_sensor_name(void)
{
#if HK_BOARD_CAMERA_OV7740
    return "OV7740";
#else
    return "OV2640";
#endif
}

hk_result_t camera_sensor_apply_init_table(void) { return SENSOR_FN(apply_init_table)(); }
hk_result_t camera_sensor_apply_common_tuning(void) { return SENSOR_FN(apply_common_tuning)(); }
hk_result_t camera_sensor_apply_output_size(uint16_t width, uint16_t height)
{
    return SENSOR_FN(apply_output_size)(width, height);
}
hk_result_t camera_sensor_apply_colorbar(uint8_t enabled)
{
    return SENSOR_FN(apply_colorbar)(enabled);
}
hk_result_t camera_sensor_select_bank(uint8_t bank)
{
#if HK_BOARD_CAMERA_OV7740
    /* OV7740 has a flat register file. A debug bank must never write 0xFF. */
    (void)bank;
    return HK_OK;
#else
    return ov2640_write_reg(0xFFU, bank);
#endif
}
hk_result_t camera_sensor_read_reg(uint8_t reg, uint8_t *out) { return SENSOR_FN(read_reg)(reg, out); }

camera_input_format_t camera_sensor_input_format(void)
{
#if HK_BOARD_CAMERA_OV7740
    return CAMERA_INPUT_YUV422;
#else
    return CAMERA_INPUT_RGB565;
#endif
}
