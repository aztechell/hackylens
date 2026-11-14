#ifndef HK_APRILTAG_DETECTOR_H
#define HK_APRILTAG_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#include "apriltag_types.h"

uint8_t apriltag_detector_init(void);
void apriltag_detector_deinit(void);
void apriltag_detector_service_tick(void);
uint8_t apriltag_detector_submit(const volatile uint16_t *pixels, uint16_t width, uint16_t height,
                                 const apriltag_scan_region_t *region);
void apriltag_detector_set_refine_edges(uint8_t enabled);
const apriltag_result_t *apriltag_detector_results(uint8_t *count, uint32_t *sequence);
void apriltag_detector_discard_results(void);
void apriltag_detector_format_info(char *line, size_t line_size);

#endif
