#ifndef HK_OV7740_SENSOR_H
#define HK_OV7740_SENSOR_H

#include <stdint.h>
void ov7740_init_bus(uint32_t *actual_xclk, uint32_t *actual_sccb);
void ov7740_probe_id(uint16_t *mid, uint16_t *pid);
uint8_t ov7740_read_reg(uint8_t reg);
void ov7740_apply_init_table(void);
void ov7740_apply_common_tuning(void);
void ov7740_apply_output_size(uint16_t width, uint16_t height);
void ov7740_apply_colorbar(uint8_t enabled);

#endif
