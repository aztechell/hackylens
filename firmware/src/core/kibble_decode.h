#ifndef HK_KIBBLE_DECODE_H
#define HK_KIBBLE_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include "kibble_model_contract.h"

enum
{
    KIBBLE_VOCAB = 2048,
    KIBBLE_PAD = 0,
    KIBBLE_BOS = 1,
    KIBBLE_EOS = 2,
    KIBBLE_WIDTH = KIBBLE_MODEL_GRID_WIDTH,
    KIBBLE_HEIGHT = KIBBLE_MODEL_GRID_HEIGHT,
    KIBBLE_CONTEXT = KIBBLE_WIDTH * KIBBLE_HEIGHT,
    KIBBLE_EMBED = 32,
    KIBBLE_FEATURES = KIBBLE_MODEL_FEATURES,
    KIBBLE_INPUT_BYTES = KIBBLE_EMBED * KIBBLE_CONTEXT,
    KIBBLE_OUTPUT_BYTES = sizeof(float) * KIBBLE_FEATURES * KIBBLE_CONTEXT,
    KIBBLE_PIECE_BYTES = 17,
    KIBBLE_PROMPT_BYTES = 24,
    KIBBLE_GENERATED_MAX = 48,
    KIBBLE_ASCII_FIRST = 32,
    KIBBLE_ASCII_COUNT = 95,
    KIBBLE_MERGES = KIBBLE_VOCAB - KIBBLE_ASCII_COUNT - 3,
};

typedef struct
{
    uint16_t left, right, token, rank;
} kibble_merge_t;

/* KLB1 CPU model, little endian. The firmware checks the CRC from the
 * generated firmware descriptor before interpreting any table from SD. */
typedef struct
{
    char magic[4];
    uint32_t model_crc;
    char pieces[KIBBLE_VOCAB][KIBBLE_PIECE_BYTES];
    uint16_t ascii[KIBBLE_ASCII_COUNT];
    uint16_t reserved;
    kibble_merge_t merges[KIBBLE_MERGES];
    uint8_t embeddings[KIBBLE_VOCAB][KIBBLE_EMBED];
    int8_t weights[KIBBLE_VOCAB][KIBBLE_FEATURES];
    float scale[KIBBLE_VOCAB];
    float bias[KIBBLE_VOCAB];
} kibble_cpu_model_t;

int kibble_tokenize(const kibble_cpu_model_t *model, const char *prompt,
                    uint16_t context[KIBBLE_CONTEXT]);
void kibble_input(const kibble_cpu_model_t *model, const uint16_t context[KIBBLE_CONTEXT],
                  uint8_t input[KIBBLE_INPUT_BYTES]);
uint16_t kibble_sample(const kibble_cpu_model_t *model, const float *output, uint32_t *seed);
void kibble_append(uint16_t context[KIBBLE_CONTEXT], uint16_t token);

#endif
