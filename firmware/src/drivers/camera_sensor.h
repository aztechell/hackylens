#ifndef HK_CAMERA_SENSOR_H
#define HK_CAMERA_SENSOR_H

#include <stdint.h>

/* Private, build-selected sensor driver. No sensor discovery or board logic in
 * applications; DVP capture always exports the existing RGB565 frame contract. */
void camera_sensor_init_bus(uint32_t *actual_xclk, uint32_t *actual_sccb);
void camera_sensor_probe_id(uint16_t *mid, uint16_t *pid);
uint8_t camera_sensor_matches_id(uint16_t mid, uint16_t pid);
const char *camera_sensor_name(void);
void camera_sensor_apply_init_table(void);
void camera_sensor_apply_common_tuning(void);
void camera_sensor_apply_output_size(uint16_t width, uint16_t height);
void camera_sensor_apply_colorbar(uint8_t enabled);
void camera_sensor_select_bank(uint8_t bank);
uint8_t camera_sensor_read_reg(uint8_t reg);

#endif
