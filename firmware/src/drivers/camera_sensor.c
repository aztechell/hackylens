#include "camera_sensor.h"

#include "board_config.h"

#if HK_BOARD_CAMERA_OV7740
#include "ov7740_sensor.h"
#define SENSOR_FN(name) ov7740_##name
#else
#include "ov2640_sensor.h"
#define SENSOR_FN(name) ov2640_##name
#endif

void camera_sensor_init_bus(uint32_t *xclk, uint32_t *sccb)
{
    SENSOR_FN(init_bus)(xclk, sccb);
}

void camera_sensor_probe_id(uint16_t *mid, uint16_t *pid)
{
    SENSOR_FN(probe_id)(mid, pid);
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

void camera_sensor_apply_init_table(void) { SENSOR_FN(apply_init_table)(); }
void camera_sensor_apply_common_tuning(void) { SENSOR_FN(apply_common_tuning)(); }
void camera_sensor_apply_output_size(uint16_t width, uint16_t height)
{
    SENSOR_FN(apply_output_size)(width, height);
}
void camera_sensor_apply_colorbar(uint8_t enabled)
{
    SENSOR_FN(apply_colorbar)(enabled);
}
void camera_sensor_select_bank(uint8_t bank)
{
#if HK_BOARD_CAMERA_OV7740
    /* OV7740 has a flat register file. A debug bank must never write 0xFF. */
    (void)bank;
#else
    ov2640_write_reg(0xFFU, bank);
#endif
}
uint8_t camera_sensor_read_reg(uint8_t reg) { return SENSOR_FN(read_reg)(reg); }
