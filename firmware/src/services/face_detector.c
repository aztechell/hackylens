#include "face_detector.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../config/face_detect_config.h"
#include "../hal/hal_dvp.h"
#include "../hal/hal_kpu.h"
#include "../hal/hal_time.h"
#include "../storage/face_model_storage.h"

/* Exact tensor contract of kendryte-standalone-demo/face_detect/detect.kmodel. */
#define FACE_W 320U
#define FACE_H 240U
#define FACE_PIXELS (FACE_W * FACE_H)
#define FACE_INPUT_BYTES (FACE_PIXELS * 3U)
#define FACE_GRID_W 20U
#define FACE_GRID_H 15U
#define FACE_GRID_CELLS (FACE_GRID_W * FACE_GRID_H)
#define FACE_ANCHORS 5U
#define FACE_OUTPUT_BYTES (FACE_GRID_CELLS * 30U * sizeof(float))
#define FACE_CANDIDATE_MAX 64U
#define FACE_THRESHOLD 0.70f
#define FACE_NMS 0.30f

typedef struct { float x, y, w, h, p; } face_candidate_t;

typedef enum
{
    FACE_DETECT_STATE_UNLOADED = 0,
    FACE_DETECT_STATE_LOADING,
    FACE_DETECT_STATE_READY,
    FACE_DETECT_STATE_RUNNING,
    FACE_DETECT_STATE_UNLOAD_PENDING,
    FACE_DETECT_STATE_FAULT,
} face_detect_state_t;

static const float g_anchors[10] = {1.889f, 2.5245f, 2.9465f, 3.94056f, 3.99987f,
                                    5.3658f, 5.155437f, 6.92275f, 6.718375f, 9.01025f};
static uint8_t *g_model;
static uint32_t g_model_size;
static volatile uint8_t g_done;
static volatile face_detect_state_t g_state = FACE_DETECT_STATE_UNLOADED;
static uint32_t g_last_us;
static uint32_t g_candidates_count;
static uint32_t g_input_dma_bytes;
static uint32_t g_start_count;
static uint64_t g_started_us;
static uint64_t g_unload_requested_us;
static face_detect_load_result_t g_result = FACE_DETECT_LOAD_FORMAT;
/* DVP AI addresses are allocated by Canaan's iomem allocator in 256-byte
   blocks.  Preserve that hardware alignment for the static equivalent. */
static uint8_t g_input[FACE_INPUT_BYTES] __attribute__((aligned(256)));
static face_candidate_t g_candidates[FACE_CANDIDATE_MAX];
static face_detect_box_t g_boxes[FACE_DETECT_BOX_MAX];
static uint8_t g_box_count;
static const hal_kpu_model_contract_t g_model_contract = {
    .version = 3U,
    .flags = 1U,
    .arch = 0U,
    .layers_length = 24U,
    .max_start_address = 0x4400U,
    .main_mem_usage = 0xafc8U,
    .output_count = 1U,
    .input_bytes = FACE_INPUT_BYTES,
};

static uint8_t *face_input_uncached(void)
{
    uintptr_t address = (uintptr_t)g_input;
    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (uint8_t *)address;
}

static float sigmoidf_fast(float value) { return 1.0f / (1.0f + expf(-value)); }

static float overlap(float c1, float s1, float c2, float s2)
{
    float left = (c1 - s1 * .5f) > (c2 - s2 * .5f) ? (c1 - s1 * .5f) : (c2 - s2 * .5f);
    float right = (c1 + s1 * .5f) < (c2 + s2 * .5f) ? (c1 + s1 * .5f) : (c2 + s2 * .5f);
    return right > left ? right - left : 0.0f;
}

static float candidate_iou(const face_candidate_t *a, const face_candidate_t *b)
{
    float intersection = overlap(a->x, a->w, b->x, b->w) * overlap(a->y, a->h, b->y, b->h);
    float total = a->w * a->h + b->w * b->h - intersection;
    return total > 0.0f ? intersection / total : 0.0f;
}

static int16_t coordinate(float normalized, uint16_t limit)
{
    int32_t value = (int32_t)(normalized * limit);
    if(value < 0) return 0;
    if(value > limit) return (int16_t)limit;
    return (int16_t)value;
}

static void decode_output(const float *output, size_t bytes)
{
    uint8_t used[FACE_CANDIDATE_MAX] = {0};

    g_candidates_count = 0;
    g_box_count = 0;
    if(!output || bytes != FACE_OUTPUT_BYTES)
        return;
    for(uint32_t anchor = 0; anchor < FACE_ANCHORS; anchor++)
    {
        uint32_t base = anchor * FACE_GRID_CELLS * 6U;
        for(uint32_t cell = 0; cell < FACE_GRID_CELLS; cell++)
        {
            float p = sigmoidf_fast(output[base + FACE_GRID_CELLS * 4U + cell]);
            face_candidate_t *candidate;
            if(p < FACE_THRESHOLD || g_candidates_count >= FACE_CANDIDATE_MAX)
                continue;
            candidate = &g_candidates[g_candidates_count++];
            candidate->x = ((cell % FACE_GRID_W) + sigmoidf_fast(output[base + cell])) / FACE_GRID_W;
            candidate->y = ((cell / FACE_GRID_W) + sigmoidf_fast(output[base + FACE_GRID_CELLS + cell])) / FACE_GRID_H;
            candidate->w = expf(output[base + FACE_GRID_CELLS * 2U + cell]) * g_anchors[anchor * 2U] / FACE_GRID_W;
            candidate->h = expf(output[base + FACE_GRID_CELLS * 3U + cell]) * g_anchors[anchor * 2U + 1U] / FACE_GRID_H;
            candidate->p = p;
        }
    }
    while(g_box_count < FACE_DETECT_BOX_MAX)
    {
        int32_t best = -1;
        float best_p = FACE_THRESHOLD;
        for(uint32_t i = 0; i < g_candidates_count; i++)
        {
            uint8_t suppressed = used[i];
            if(g_candidates[i].p <= best_p || suppressed)
                continue;
            for(uint8_t box_index = 0; box_index < g_box_count; box_index++)
            {
                face_candidate_t kept;
                const face_detect_box_t *box = &g_boxes[box_index];
                kept.x = (box->x + box->w * .5f) / FACE_W;
                kept.y = (box->y + box->h * .5f) / FACE_H;
                kept.w = box->w / (float)FACE_W;
                kept.h = box->h / (float)FACE_H;
                if(candidate_iou(&g_candidates[i], &kept) > FACE_NMS)
                {
                    suppressed = 1;
                    break;
                }
            }
            if(!suppressed) { best = (int32_t)i; best_p = g_candidates[i].p; }
        }
        if(best < 0)
            break;
        else
        {
            const face_candidate_t *candidate = &g_candidates[best];
            face_detect_box_t *box = &g_boxes[g_box_count];
            int16_t x1 = coordinate(candidate->x - candidate->w * .5f, FACE_W);
            int16_t y1 = coordinate(candidate->y - candidate->h * .5f, FACE_H);
            int16_t x2 = coordinate(candidate->x + candidate->w * .5f, FACE_W);
            int16_t y2 = coordinate(candidate->y + candidate->h * .5f, FACE_H);
            used[best] = 1;
            if(x2 > x1 && y2 > y1)
            {
                box->x = x1; box->y = y1; box->w = x2 - x1; box->h = y2 - y1;
                g_box_count++;
            }
        }
    }
}

static void inference_done(void *userdata)
{
    (void)userdata;
    g_done = 1;
    if(g_state == FACE_DETECT_STATE_RUNNING)
        g_state = FACE_DETECT_STATE_READY;
}

static void finish_inference(void)
{
    const uint8_t *output;
    size_t bytes;
    if(!g_done) return;
    g_done = 0;
    g_last_us = (uint32_t)(hal_time_us() - g_started_us);
    if(hal_kpu_get_output(0, &output, &bytes) == 0)
        decode_output((const float *)output, bytes);
    /* Keep the DVP's RGB planes immutable while KPU uses them, then arm it
       for the next completed camera frame. */
    hal_dvp_output_ai(1);
}

static void reset_runtime_state(void)
{
    g_done = 0;
    g_model = NULL;
    g_model_size = 0;
    g_last_us = 0;
    g_candidates_count = 0;
    g_input_dma_bytes = 0;
    g_start_count = 0;
    g_started_us = 0;
    g_unload_requested_us = 0;
    g_box_count = 0;
    memset(g_input, 0, sizeof(g_input));
    memset(g_candidates, 0, sizeof(g_candidates));
    memset(g_boxes, 0, sizeof(g_boxes));
    g_state = FACE_DETECT_STATE_UNLOADED;
}

static uint8_t release_resources(void)
{
    uint8_t *model;

    if(!hal_kpu_model_unload())
        return 0;
    model = g_model;
    if(model)
        face_model_storage_free(model);
    reset_runtime_state();
    return 1;
}

face_detect_load_result_t face_detector_load(void)
{
    face_model_storage_result_t storage;
    hal_kpu_load_result_t load_result;

    face_detector_service_tick();
    if(g_state != FACE_DETECT_STATE_UNLOADED || hal_kpu_is_busy() || hal_kpu_is_loaded())
    {
        g_result = FACE_DETECT_LOAD_KPU;
        return g_result;
    }
    if(!release_resources())
    {
        g_state = FACE_DETECT_STATE_FAULT;
        g_result = FACE_DETECT_LOAD_KPU;
        return g_result;
    }
    g_state = FACE_DETECT_STATE_LOADING;
    storage = face_model_storage_load(&g_model, &g_model_size);
    if(storage != FACE_MODEL_STORAGE_OK)
    {
        face_detect_load_result_t result =
            storage == FACE_MODEL_STORAGE_NO_SD ? FACE_DETECT_LOAD_NO_SD :
            storage == FACE_MODEL_STORAGE_DIR ? FACE_DETECT_LOAD_DIR :
            storage == FACE_MODEL_STORAGE_FILE ? FACE_DETECT_LOAD_FILE :
            storage == FACE_MODEL_STORAGE_ALLOC ? FACE_DETECT_LOAD_ALLOC : FACE_DETECT_LOAD_READ;
        release_resources();
        g_result = result;
        return g_result;
    }
    load_result = hal_kpu_model_load(g_model, g_model_size, &g_model_contract, &g_input_dma_bytes);
    if(load_result != HAL_KPU_LOAD_OK)
    {
        face_detect_load_result_t result =
            load_result == HAL_KPU_LOAD_FORMAT ? FACE_DETECT_LOAD_FORMAT : FACE_DETECT_LOAD_KPU;
        release_resources();
        g_result = result;
        return g_result;
    }
    g_state = FACE_DETECT_STATE_READY;
    g_result = FACE_DETECT_LOAD_OK;
    printf("[FACE] KPU-V3 model=%08X align=%u bytes=%u input=%u, 320x240 YOLO\r\n",
           (unsigned)(uintptr_t)g_model, (unsigned)((uintptr_t)g_model & 127U),
           (unsigned)g_model_size, (unsigned)g_input_dma_bytes);
    return g_result;
}

void face_detector_unload(void)
{
    hal_dvp_output_ai(0);
    if(g_state == FACE_DETECT_STATE_RUNNING)
    {
        g_state = FACE_DETECT_STATE_UNLOAD_PENDING;
        g_unload_requested_us = hal_time_us();
        return;
    }
    if(g_state == FACE_DETECT_STATE_READY || g_state == FACE_DETECT_STATE_LOADING)
        release_resources();
}

void face_detector_service_tick(void)
{
    hal_kpu_stop_result_t stop_result;

    if(g_state != FACE_DETECT_STATE_UNLOAD_PENDING)
        return;
    if(!hal_kpu_is_busy())
    {
        release_resources();
        return;
    }
    if(hal_time_us() - g_unload_requested_us < FACE_DETECT_UNLOAD_TIMEOUT_US)
        return;

    printf("[FACE] unload timeout, stopping KPU\r\n");
    stop_result = hal_kpu_stop_and_reset();
    if(stop_result == HAL_KPU_STOP_OK || stop_result == HAL_KPU_STOP_NOT_RUNNING)
    {
        release_resources();
        return;
    }
    g_state = FACE_DETECT_STATE_FAULT;
    g_result = FACE_DETECT_LOAD_KPU;
    printf("[FACE] KPU stop failed, reboot required\r\n");
}

uint8_t face_detector_ready(void)
{
    return g_result == FACE_DETECT_LOAD_OK &&
           (g_state == FACE_DETECT_STATE_READY || g_state == FACE_DETECT_STATE_RUNNING) &&
           hal_kpu_is_loaded();
}

void face_detector_attach_camera(void)
{
    uint8_t *input = face_input_uncached();

    /* This is the exact path from Canaan's face_detect demo: DVP writes its
       native planar R8/G8/B8 frame directly into the KPU input buffer. */
    hal_dvp_set_ai_rgb888((uint32_t)(uintptr_t)input,
                          (uint32_t)(uintptr_t)(input + FACE_PIXELS),
                          (uint32_t)(uintptr_t)(input + FACE_PIXELS * 2U));
}

void face_detector_process_frame(void)
{
    uint8_t *input = face_input_uncached();
    uint8_t inference_finished;
    if(!face_detector_ready()) return;
    inference_finished = g_done;
    finish_inference();
    /* The just-finished UI frame was captured with AI output disabled; wait
       for the following frame after re-arming DVP above. */
    if(inference_finished || hal_kpu_is_busy()) return;
    g_started_us = hal_time_us();
    hal_dvp_output_ai(0);
    if(g_start_count++ == 0U)
        printf("[FACE] kpu start input=%08X bytes=%u\r\n",
               (unsigned)(uintptr_t)input, (unsigned)g_input_dma_bytes);
    g_state = FACE_DETECT_STATE_RUNNING;
    if(hal_kpu_run(input, inference_done, NULL) != 0)
    {
        g_state = FACE_DETECT_STATE_READY;
        hal_dvp_output_ai(1);
        printf("[FACE] kpu start failed\r\n");
    }
}

const face_detect_box_t *face_detector_boxes(uint8_t *count)
{
    if(count) *count = g_box_count;
    return g_boxes;
}

const char *face_detector_error_label(face_detect_load_result_t result)
{
    static const char *labels[] = {"NONE", "NO SD", "MODEL DIR", "MODEL FILE", "MODEL READ", "MODEL ALLOC", "MODEL FORMAT", "MODEL LOAD"};
    return result <= FACE_DETECT_LOAD_KPU ? labels[result] : "MODEL LOAD";
}

void face_detector_format_info(char *line, size_t line_size)
{
    static const char *states[] = {"UNLOADED", "LOADING", "READY", "RUNNING", "UNLOAD_PENDING", "FAULT"};
    snprintf(line, line_size, "HKFACEINFO state=%s error=%s size=%u KPU-V3 320x240 grid=20x15x30 us=%u candidates=%u boxes=%u\r\n",
             states[g_state], face_detector_error_label(g_result), (unsigned)g_model_size,
             (unsigned)g_last_us, (unsigned)g_candidates_count, (unsigned)g_box_count);
}
