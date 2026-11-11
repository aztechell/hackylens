#ifndef HK_AI_MODEL_RUNTIME_H
#define HK_AI_MODEL_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "../core/ai_model_types.h"

typedef enum
{
    AI_MODEL_STATE_UNLOADED = 0,
    AI_MODEL_STATE_LOADING,
    AI_MODEL_STATE_READY,
    AI_MODEL_STATE_RUNNING,
    AI_MODEL_STATE_UNLOAD_PENDING,
    AI_MODEL_STATE_FAULT,
} ai_model_state_t;

typedef enum
{
    AI_MODEL_RESULT_OK = 0,
    AI_MODEL_RESULT_NO_SD,
    AI_MODEL_RESULT_DIRECTORY,
    AI_MODEL_RESULT_FILE,
    AI_MODEL_RESULT_SIZE,
    AI_MODEL_RESULT_ALLOC,
    AI_MODEL_RESULT_READ,
    AI_MODEL_RESULT_MANIFEST,
    AI_MODEL_RESULT_DESCRIPTOR,
    AI_MODEL_RESULT_FORMAT,
    AI_MODEL_RESULT_OUTPUT,
    AI_MODEL_RESULT_BUSY,
    AI_MODEL_RESULT_KPU,
    AI_MODEL_RESULT_RUN,
    AI_MODEL_RESULT_STOP,
    AI_MODEL_RESULT_PENDING,
} ai_model_result_t;

typedef struct
{
    const ai_model_descriptor_t *descriptor;
    uint8_t *model;
    uint8_t *scratch;
    uint32_t model_size;
    uint32_t model_crc32;
    uint32_t input_dma_bytes;
    uint32_t run_count;
    uint32_t last_inference_us;
    uint64_t started_us;
    uint64_t unload_requested_us;
    uint64_t unload_deadline_us;
    volatile ai_model_state_t state;
    volatile uint8_t completion_pending;
    uint8_t slot;
    uint8_t borrowed_model;
    ai_model_result_t result;
} ai_model_runtime_t;

void ai_model_runtime_init(ai_model_runtime_t *runtime);
ai_model_result_t ai_model_runtime_load(ai_model_runtime_t *runtime,
                                        const ai_model_descriptor_t *descriptor);
/* Load a companion for an existing lease. Only one model can run at a time. */
ai_model_result_t ai_model_runtime_load_related(ai_model_runtime_t *runtime,
                                                const ai_model_descriptor_t *descriptor,
                                                const ai_model_runtime_t *primary,
                                                const ai_model_workspace_t *workspace);
/* Call on successive ticks until the model is ready or loading fails. */
ai_model_result_t ai_model_runtime_load_related_step(ai_model_runtime_t *runtime,
                                                     const ai_model_descriptor_t *descriptor,
                                                     const ai_model_runtime_t *primary,
                                                     const ai_model_workspace_t *workspace);
uint8_t ai_model_runtime_run(ai_model_runtime_t *runtime, const uint8_t *input);
uint8_t ai_model_runtime_take_completion(ai_model_runtime_t *runtime);
int ai_model_runtime_get_output(ai_model_runtime_t *runtime, uint32_t index,
                                const uint8_t **output, size_t *bytes);
void ai_model_runtime_request_unload(ai_model_runtime_t *runtime);
void ai_model_runtime_limit_unload(ai_model_runtime_t *runtime, uint64_t deadline_us);
void ai_model_runtime_tick(ai_model_runtime_t *runtime);
uint8_t ai_model_runtime_loaded(const ai_model_runtime_t *runtime);
uint8_t ai_model_runtime_busy(const ai_model_runtime_t *runtime);
const char *ai_model_runtime_state_label(ai_model_state_t state);
const char *ai_model_runtime_result_label(ai_model_result_t result);

#endif
