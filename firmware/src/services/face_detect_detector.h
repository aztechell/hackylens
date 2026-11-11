#ifndef HK_FACE_DETECT_DETECTOR_H
#define HK_FACE_DETECT_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#include "ai_model_runtime.h"

#define FACE_DETECT_BOX_MAX 8U
#define FACE_DETECT_INPUT_WIDTH 320U
#define FACE_DETECT_INPUT_HEIGHT 240U

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
    FACE_DETECT_LOAD_BUSY,
} face_detect_load_result_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} face_detect_box_t;

/* A consumer can hold the detector's input for follow-up inference.
 * Call resume when finished. Box arrays are valid only in the callback. */
typedef uint8_t (*face_detect_consumer_t)(const uint8_t *rgb_planes,
                                         const face_detect_box_t *model_boxes,
                                         const face_detect_box_t *sensor_boxes,
                                         uint8_t count, void *context);

face_detect_load_result_t face_detect_detector_load(const ai_model_workspace_t *workspace);
void face_detect_detector_unload(void);
void face_detect_detector_limit_unload(uint64_t deadline_us);
void face_detect_detector_service_tick(void);
uint8_t face_detect_detector_ready(void);
face_detect_load_result_t face_detect_detector_result(void);
void face_detect_detector_attach_camera(void);
void face_detect_detector_process_frame(face_detect_consumer_t consumer, void *context);
void face_detect_detector_resume(void);
const ai_model_runtime_t *face_detect_detector_model(void);
const face_detect_box_t *face_detect_detector_boxes(uint8_t *count);
const char *face_detect_detector_error_label(face_detect_load_result_t result);
void face_detect_detector_format_info(char *line, size_t line_size);

#endif
