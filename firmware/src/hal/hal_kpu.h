#ifndef HAL_KPU_H
#define HAL_KPU_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    HAL_KPU_LOAD_OK = 0,
    HAL_KPU_LOAD_FORMAT,
    HAL_KPU_LOAD_FAILED,
} hal_kpu_load_result_t;

typedef enum
{
    HAL_KPU_STOP_OK = 0,
    HAL_KPU_STOP_NOT_RUNNING,
    HAL_KPU_STOP_UNSUPPORTED,
    HAL_KPU_STOP_FAILED,
} hal_kpu_stop_result_t;

typedef struct
{
    uint32_t version;
    uint32_t flags;
    uint32_t arch;
    uint32_t layers_length;
    uint32_t max_start_address;
    uint32_t main_mem_usage;
    uint32_t output_count;
    uint32_t input_bytes;
} hal_kpu_model_contract_t;

typedef void (*hal_kpu_done_callback_t)(void *userdata);

hal_kpu_load_result_t hal_kpu_model_load(const uint8_t *model, uint32_t model_size,
                                         const hal_kpu_model_contract_t *contract,
                                         uint32_t *input_bytes);
int hal_kpu_run(const uint8_t *input, hal_kpu_done_callback_t callback, void *userdata);
int hal_kpu_get_output(uint32_t index, const uint8_t **output, size_t *bytes);
uint8_t hal_kpu_is_busy(void);
uint8_t hal_kpu_is_loaded(void);
hal_kpu_stop_result_t hal_kpu_stop_and_reset(void);
uint8_t hal_kpu_model_unload(void);

#endif
