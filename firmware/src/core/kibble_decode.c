#include "kibble_decode.h"

#include <math.h>
#include <string.h>

_Static_assert(sizeof(kibble_merge_t) == 8, "KLB1 merge record");
_Static_assert(sizeof(kibble_cpu_model_t) == KIBBLE_CPU_BYTES, "KLB1 size");
_Static_assert(KIBBLE_MODEL_CENTER < KIBBLE_CONTEXT, "feature grid center");
_Static_assert(KIBBLE_CONTEXT > KIBBLE_PROMPT_BYTES, "context must fit a prompt and BOS");

static const kibble_merge_t *lookup(const kibble_cpu_model_t *model, uint16_t left, uint16_t right)
{
    uint32_t key = ((uint32_t)left << 16) | right;
    unsigned begin = 0;
    unsigned end = KIBBLE_MERGES;
    while(begin < end)
    {
        unsigned mid = begin + (end - begin) / 2U;
        const kibble_merge_t *candidate = &model->merges[mid];
        uint32_t candidate_key = ((uint32_t)candidate->left << 16) | candidate->right;
        if(candidate_key == key)
            return candidate;
        if(candidate_key < key)
            begin = mid + 1;
        else
            end = mid;
    }
    return NULL;
}

int kibble_tokenize(const kibble_cpu_model_t *model, const char *prompt,
                    uint16_t context[KIBBLE_CONTEXT])
{
    uint16_t tokens[KIBBLE_PROMPT_BYTES];
    size_t count = 0;
    for(; prompt[count]; ++count)
    {
        unsigned ch = (uint8_t)prompt[count];
        if(count == KIBBLE_PROMPT_BYTES || ch < KIBBLE_ASCII_FIRST ||
           ch >= KIBBLE_ASCII_FIRST + KIBBLE_ASCII_COUNT)
            return 0;
        tokens[count] = model->ascii[ch - KIBBLE_ASCII_FIRST];
    }
    for(;;)
    {
        const kibble_merge_t *best = NULL;
        size_t pair = 0;
        for(size_t i = 1; i < count; ++i)
        {
            const kibble_merge_t *candidate = lookup(model, tokens[i - 1], tokens[i]);
            if(candidate && (!best || candidate->rank < best->rank))
            {
                best = candidate;
                pair = i - 1;
            }
        }
        if(!best)
            break;
        tokens[pair] = best->token;
        memmove(tokens + pair + 1, tokens + pair + 2, (--count - pair - 1) * sizeof(tokens[0]));
    }
    memset(context, 0, KIBBLE_CONTEXT * sizeof(context[0]));
    context[KIBBLE_CONTEXT - count - 1] = KIBBLE_BOS;
    memcpy(context + KIBBLE_CONTEXT - count, tokens, count * sizeof(tokens[0]));
    return 1;
}

void kibble_input(const kibble_cpu_model_t *model, const uint16_t context[KIBBLE_CONTEXT],
                  uint8_t input[KIBBLE_INPUT_BYTES])
{
    static const uint8_t layout[KIBBLE_CONTEXT] = KIBBLE_MODEL_LAYOUT;
    for(unsigned cell = 0; cell < KIBBLE_CONTEXT; ++cell)
    {
        const uint8_t *embedding = model->embeddings[context[layout[cell]]];
        for(unsigned c = 0; c < KIBBLE_EMBED; ++c)
            input[c * KIBBLE_CONTEXT + cell] = embedding[c];
    }
}

uint16_t kibble_sample(const kibble_cpu_model_t *model, const float *output, uint32_t *seed)
{
    enum { TOP = 16, CENTER = KIBBLE_MODEL_CENTER };
    float maximum = 0;
    for(unsigned c = 0; c < KIBBLE_FEATURES; ++c)
        maximum = fmaxf(maximum, output[c * KIBBLE_CONTEXT + CENTER]);
    float feature_scale = maximum > 0 ? maximum / 255.0f : 1.0f;
    uint8_t features[KIBBLE_FEATURES];
    for(unsigned c = 0; c < KIBBLE_FEATURES; ++c)
    {
        float value = output[c * KIBBLE_CONTEXT + CENTER] / feature_scale;
        features[c] = (uint8_t)(fminf(255.0f, fmaxf(0.0f, value)) + 0.5f);
    }

    float logits[TOP];
    uint16_t tokens[TOP] = {0};
    for(unsigned i = 0; i < TOP; ++i)
        logits[i] = -INFINITY;
    for(unsigned token = KIBBLE_EOS; token < KIBBLE_VOCAB; ++token)
    {
        int32_t dot = 0;
        for(unsigned c = 0; c < KIBBLE_FEATURES; ++c)
            dot += features[c] * model->weights[token][c];
        float logit = (float)dot * feature_scale * model->scale[token] + model->bias[token];
        if(logit <= logits[TOP - 1])
            continue;
        unsigned i = TOP - 1;
        while(i && logit > logits[i - 1])
        {
            logits[i] = logits[i - 1];
            tokens[i] = tokens[i - 1];
            --i;
        }
        logits[i] = logit;
        tokens[i] = (uint16_t)token;
    }
    float total = 0;
    float top = logits[0];
    for(unsigned i = 0; i < TOP; ++i)
    {
        total += expf((logits[i] - top) / 0.85f);
        logits[i] = total;
    }
    uint32_t random = *seed ? *seed : 210U;
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    *seed = random;
    float threshold = (float)(random >> 8) * (1.0f / 16777216.0f) * total;
    unsigned choice = 0;
    while(choice < TOP - 1 && threshold >= logits[choice])
        ++choice;
    return tokens[choice];
}

void kibble_append(uint16_t context[KIBBLE_CONTEXT], uint16_t token)
{
    memmove(context, context + 1, (KIBBLE_CONTEXT - 1) * sizeof(context[0]));
    context[KIBBLE_CONTEXT - 1] = token;
}
