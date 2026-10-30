#ifndef HAL_CAMERA_BUS_H
#define HAL_CAMERA_BUS_H
#include <stdint.h>
#include "../../../firmware/include/hackylens/capability/common.h"
/* Private build-selected register transport; all deadlines are absolute. */
typedef struct {
    uint32_t configured_hz; /* requested I2C rate, not a measurement; zero for SCCB */
    uint8_t address_7bit;
    uint8_t scl_low_count;
    uint8_t scl_high_count;
} hal_camera_bus_info_t;
hk_result_t hal_camera_bus_init(hal_camera_bus_info_t *info, uint64_t deadline_us);
hk_result_t hal_camera_bus_read8(uint8_t reg, uint8_t *out, uint64_t deadline_us);
hk_result_t hal_camera_bus_write8(uint8_t reg, uint8_t value, uint64_t deadline_us);
#endif
