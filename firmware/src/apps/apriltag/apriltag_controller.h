#ifndef HK_APRILTAG_CONTROLLER_H
#define HK_APRILTAG_CONTROLLER_H

#include "../../core/hk_app.h"
#include <hackylens/capability/accelerometer.h>

void apriltag_controller_enter(const hk_input_snapshot_t *input, const hk_accelerometer_t *sensor);
void apriltag_controller_exit(void);
void apriltag_controller_tick(const hk_input_snapshot_t *input);
uint8_t apriltag_controller_handle_buttons(const hk_input_snapshot_t *input);
int16_t apriltag_controller_target_id(void);
uint8_t apriltag_controller_has_accelerometer(void);
uint8_t apriltag_controller_capture_gravity(uint8_t lens_down);
uint8_t apriltag_controller_arm_calibration(void);
uint8_t apriltag_controller_calibration_armed(void);
void apriltag_controller_cancel_calibration(void);
uint32_t apriltag_controller_range_mm(void);
uint32_t apriltag_controller_horizontal_mm(void);
void apriltag_controller_stats(uint32_t *preview_count, uint32_t *read_count);

#endif
