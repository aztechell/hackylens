#ifndef HK_AI_MODEL_STORAGE_H
#define HK_AI_MODEL_STORAGE_H

#include <stdint.h>

#include "../core/ai_model_types.h"
#include "fat32_stream.h"

typedef enum
{
    AI_MODEL_STORAGE_OK = 0,
    AI_MODEL_STORAGE_NO_SD,
    AI_MODEL_STORAGE_DIRECTORY,
    AI_MODEL_STORAGE_FILE,
    AI_MODEL_STORAGE_SIZE,
    AI_MODEL_STORAGE_ALLOC,
    AI_MODEL_STORAGE_READ,
    AI_MODEL_STORAGE_MANIFEST,
    AI_MODEL_STORAGE_PENDING,
} ai_model_storage_result_t;

typedef struct
{
    fat_stream_t stream;
    fat_file_entry_t entry;
    uint32_t position;
} ai_model_reader_t;

ai_model_storage_result_t ai_model_storage_load(const char *directory_path,
                                                const char *model_path,
                                                uint32_t max_bytes,
                                                uint32_t alignment,
                                                uint32_t scratch_bytes,
                                                const ai_model_workspace_t *workspace,
                                                uint8_t **buffer,
                                                uint32_t *size,
                                                uint32_t *crc32,
                                                ai_model_reader_t *reader);
ai_model_storage_result_t ai_model_storage_read_step(ai_model_reader_t *reader,
                                                      uint8_t *buffer, uint32_t size,
                                                      uint32_t *crc32);
void ai_model_storage_free(uint8_t *buffer);
ai_model_storage_result_t ai_model_storage_load_manifest(const char *manifest_path,
                                                         ai_model_manifest_t *manifest);
ai_model_storage_result_t ai_model_storage_validate_labels(
    const char *labels_path, uint16_t expected_count);

#endif
