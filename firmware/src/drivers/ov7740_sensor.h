#ifndef HK_OV7740_SENSOR_H
#define HK_OV7740_SENSOR_H

#include <stdint.h>
#include "hal_camera_bus.h"
hk_result_t ov7740_probe_id(uint16_t *mid, uint16_t *pid);
hk_result_t ov7740_read_reg(uint8_t reg, uint8_t *out);
hk_result_t ov7740_apply_init_table(void);
hk_result_t ov7740_apply_common_tuning(void);
hk_result_t ov7740_apply_output_size(uint16_t width, uint16_t height);
hk_result_t ov7740_apply_colorbar(uint8_t enabled);

#endif
