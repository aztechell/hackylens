#ifndef HK_FACE_DETECTOR_H
#define HK_FACE_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#define FACE_DETECT_BOX_MAX 8U

typedef enum
{
    FACE_DETECT_LOAD_OK = 0,
    FACE_DETECT_LOAD_NO_SD,
    FACE_DETECT_LOAD_DIR,
    FACE_DETECT_LOAD_FILE,
    FACE_DETECT_LOAD_READ,
    FACE_DETECT_LOAD_ALLOC,
    FACE_DETECT_LOAD_FORMAT,
    FACE_DETECT_LOAD_KPU,
} face_detect_load_result_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} face_detect_box_t;

face_detect_load_result_t face_detector_load(void);
void face_detector_unload(void);
void face_detector_service_tick(void);
uint8_t face_detector_ready(void);
void face_detector_attach_camera(void);
void face_detector_process_frame(void);
const face_detect_box_t *face_detector_boxes(uint8_t *count);
const char *face_detector_error_label(face_detect_load_result_t result);
void face_detector_format_info(char *line, size_t line_size);

#endif
