#include "hk_config.h"

#if HK_ENABLE_APP_KIBBLE_LM
#include <stdio.h>
#include <string.h>

#include <hackylens/app/kibble.h>

#include "app_services.h"
#include "../core/kibble_decode.h"
#include "../services/ai_model_runtime.h"
#include "../services/resource_cleanup.h"
#include "../storage/ai_model_storage.h"

#define KIBBLE_DIRECTORY KIBBLE_MODEL_DIRECTORY

static const ai_model_descriptor_t s_descriptor = {
    .id = "kibblelm",
    .directory_path = KIBBLE_DIRECTORY,
    .model_path = KIBBLE_DIRECTORY "/model.kmodel",
    .max_model_bytes = KIBBLE_MODEL_BYTES,
    .expected_model_crc32 = KIBBLE_MODEL_CRC,
    .unload_timeout_us = 250000U,
    .kmodel = {
        .version = 4U, .arch = 1U, .flags = KIBBLE_MODEL_FLAGS,
        .layers_length = KIBBLE_MODEL_NODES, .main_mem_usage = KIBBLE_MODEL_MAIN,
        .output_count = 1U, .input_bytes = KIBBLE_MODEL_INPUT_MEMORY,
    },
    /* The loader checks the packed KPU memory extent. nncase uploads the
     * logical CHW tensor from s_input and adds the row padding itself. */
    .input = { .name = "embeddings", .bytes = KIBBLE_MODEL_INPUT_MEMORY,
               .shape = {1, KIBBLE_EMBED, KIBBLE_HEIGHT, KIBBLE_WIDTH}, .rank = 4,
               .element = AI_MODEL_ELEMENT_U8, .layout = AI_MODEL_LAYOUT_CHW },
    .outputs = {{ .name = "features", .bytes = KIBBLE_OUTPUT_BYTES,
                  .shape = {1, KIBBLE_FEATURES, KIBBLE_HEIGHT, KIBBLE_WIDTH}, .rank = 4,
                  .element = AI_MODEL_ELEMENT_F32, .layout = AI_MODEL_LAYOUT_CHW }},
    .output_count = 1U,
};

static ai_model_runtime_t s_runtime;
static ai_model_reader_t s_cpu_reader;
static uint8_t *s_cpu_bytes;
static uint32_t s_cpu_crc;
static uint32_t s_seed;
static uint16_t s_context[KIBBLE_CONTEXT];
static _Alignas(256) uint8_t s_input[KIBBLE_INPUT_BYTES];
static uint64_t s_inference_deadline;
static uint8_t s_cpu_ready, s_generating, s_inflight, s_closing;
static uint8_t s_generated;

_Static_assert((int)HK_KIBBLE_PROMPT_MAX == (int)KIBBLE_PROMPT_BYTES, "prompt contract");
_Static_assert((int)HK_KIBBLE_TOKEN_MAX + 1 == (int)KIBBLE_PIECE_BYTES, "token contract");

static hk_result_t authorized(const hk_app_context_t *ctx, uint8_t starts_work)
{
    hk_result_t result = app_service_context(ctx, starts_work);
    if(result != HK_OK)
        return result;
    const char *id;
    uint32_t generation;
    result = hk_app_context_identity(ctx, &id, &generation);
    return result == HK_OK && strcmp(id, "kibble-lm") ? HK_ERR_NOT_DECLARED : result;
}

static uint64_t now_us(const hk_app_context_t *ctx)
{
    const hk_time_t *clock;
    uint64_t now = 0;
    if(hk_app_context_time(ctx, &clock) == HK_OK)
        (void)hk_time_now_us(clock, &now);
    return now;
}

static uint8_t retire(void)
{
    ai_model_runtime_tick(&s_runtime);
    if(s_runtime.state == AI_MODEL_STATE_UNLOAD_PENDING)
        return 0U;
    s_closing = 0U;
    return 1U;
}

static void close_model(hk_deadline_t deadline)
{
    s_generating = s_inflight = s_cpu_ready = 0U;
    ai_model_storage_free(s_cpu_bytes);
    s_cpu_bytes = NULL;
    if(s_closing)
        return;
    ai_model_runtime_limit_unload(&s_runtime, deadline.at_us);
    ai_model_runtime_request_unload(&s_runtime);
    s_closing = !retire();
    if(s_closing)
        (void)resource_cleanup_schedule(RESOURCE_CLEANUP_KIBBLE, retire);
}

static hk_result_t load_failure(const char **status, const char *message)
{
    close_model(HK_DEADLINE_IMMEDIATE);
    *status = message;
    return HK_ERR_IO;
}

static hk_result_t load_cpu(const char **status)
{
    ai_model_storage_result_t result;
    if(!s_cpu_bytes)
    {
        uint32_t size;
        result = ai_model_storage_load(KIBBLE_DIRECTORY, KIBBLE_DIRECTORY "/cpu.bin",
            sizeof(kibble_cpu_model_t), 256U, 0U, NULL, &s_cpu_bytes, &size, &s_cpu_crc, &s_cpu_reader);
        if(result == AI_MODEL_STORAGE_NO_SD)
            return load_failure(status, "Insert model SD");
        if(result == AI_MODEL_STORAGE_DIRECTORY || result == AI_MODEL_STORAGE_FILE)
            return load_failure(status, "Missing model");
        if(result != AI_MODEL_STORAGE_PENDING || size != sizeof(kibble_cpu_model_t))
            return load_failure(status, "CPU package error");
        return HK_PENDING;
    }
    result = ai_model_storage_read_step(&s_cpu_reader, s_cpu_bytes, sizeof(kibble_cpu_model_t), &s_cpu_crc);
    if(result == AI_MODEL_STORAGE_PENDING)
        return HK_PENDING;
    const kibble_cpu_model_t *cpu = (const kibble_cpu_model_t *)s_cpu_bytes;
    if(result != AI_MODEL_STORAGE_OK || s_cpu_crc != KIBBLE_CPU_CRC ||
       memcmp(cpu->magic, "KLB1", 4) || cpu->model_crc != KIBBLE_MODEL_CRC)
        return load_failure(status, "CPU package invalid");
    s_cpu_ready = 1U;
    return HK_OK;
}

hk_result_t hk_app_kibble_open_step(const hk_app_context_t *ctx, const char **status)
{
    hk_result_t result = authorized(ctx, 1U);
    if(result != HK_OK || !status)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    *status = "Loading model";
    if(!KIBBLE_MODEL_CRC || !KIBBLE_CPU_CRC)
        return load_failure(status, "Model not built");
    if(s_closing)
        return HK_PENDING;
    if(s_runtime.state == AI_MODEL_STATE_FAULT)
        return load_failure(status, "KPU needs reset");
    if(!s_cpu_ready)
    {
        result = load_cpu(status);
        if(result != HK_OK)
            return result;
    }
    if(!ai_model_runtime_loaded(&s_runtime))
    {
        ai_model_result_t load = ai_model_runtime_load_related_step(&s_runtime, &s_descriptor, NULL, NULL);
        if(load == AI_MODEL_RESULT_PENDING || load == AI_MODEL_RESULT_BUSY)
            return HK_PENDING;
        if(load != AI_MODEL_RESULT_OK)
            return load_failure(status, ai_model_runtime_result_label(load));
    }
    *status = NULL;
    return HK_OK;
}

hk_result_t hk_app_kibble_start(const hk_app_context_t *ctx, const char *prompt, uint32_t seed)
{
    hk_result_t result = authorized(ctx, 1U);
    if(result != HK_OK)
        return result;
    if(s_generating || s_inflight)
        return HK_ERR_BUSY;
    if(!s_cpu_ready || !ai_model_runtime_loaded(&s_runtime))
        return HK_ERR_INVALID_STATE;
    if(!prompt || !kibble_tokenize((const kibble_cpu_model_t *)s_cpu_bytes, prompt, s_context))
        return HK_ERR_INVALID_ARGUMENT;
    s_seed = seed;
    s_generated = 0U;
    s_generating = 1U;
    return HK_OK;
}

hk_result_t hk_app_kibble_poll(const hk_app_context_t *ctx, hk_kibble_token_t *token)
{
    hk_result_t result = authorized(ctx, 1U);
    if(result != HK_OK || !token)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    if(!s_generating)
        return HK_ERR_INVALID_STATE;
    const kibble_cpu_model_t *cpu = (const kibble_cpu_model_t *)s_cpu_bytes;
    if(!s_inflight)
    {
        kibble_input(cpu, s_context, s_input);
        if(!ai_model_runtime_run(&s_runtime, s_input))
            return HK_ERR_IO;
        s_inflight = 1U;
        s_inference_deadline = now_us(ctx) + 1000000U;
        return HK_PENDING;
    }
    uint64_t started = now_us(ctx);
    if(!ai_model_runtime_take_completion(&s_runtime))
        return started < s_inference_deadline ? HK_PENDING : HK_ERR_DEADLINE_EXCEEDED;

    s_inflight = 0U;
    const uint8_t *output;
    size_t size;
    if(ai_model_runtime_get_output(&s_runtime, 0, &output, &size) || size != KIBBLE_OUTPUT_BYTES)
        return HK_ERR_IO;
    uint16_t next = kibble_sample(cpu, (const float *)output, &s_seed);
    *token = (hk_kibble_token_t){ .end = next == KIBBLE_EOS, .accelerator_us = s_runtime.last_inference_us,
                                  .cpu_us = (uint32_t)(now_us(ctx) - started) };
    if(!token->end)
    {
        memcpy(token->text, cpu->pieces[next], sizeof(token->text));
        kibble_append(s_context, next);
    }
    if(token->end || ++s_generated == HK_KIBBLE_GENERATION_MAX)
        s_generating = 0U;
    return HK_OK;
}

hk_result_t hk_app_kibble_close(const hk_app_context_t *ctx, hk_deadline_t deadline)
{
    hk_result_t result = authorized(ctx, 0U);
    if(result == HK_OK)
        close_model(deadline);
    return result;
}
#endif
