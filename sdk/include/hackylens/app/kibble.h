#ifndef HACKYLENS_APP_KIBBLE_H
#define HACKYLENS_APP_KIBBLE_H

#include <hackylens/app/context.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { HK_KIBBLE_PROMPT_MAX = 24, HK_KIBBLE_TOKEN_MAX = 16, HK_KIBBLE_GENERATION_MAX = 48 };

typedef struct
{
    char text[HK_KIBBLE_TOKEN_MAX + 1];
    uint32_t accelerator_us;
    uint32_t cpu_us;
    uint8_t end;
} hk_kibble_token_t;

/* All calls are scoped to the built-in KibbleLM app's current callback.
 * Open and poll return HK_PENDING while their work is in flight. */
hk_result_t hk_app_kibble_open_step(const hk_app_context_t *ctx, const char **status);
hk_result_t hk_app_kibble_start(const hk_app_context_t *ctx, const char *prompt, uint32_t seed);
hk_result_t hk_app_kibble_poll(const hk_app_context_t *ctx, hk_kibble_token_t *token);
hk_result_t hk_app_kibble_close(const hk_app_context_t *ctx, hk_deadline_t deadline);

#ifdef __cplusplus
}
#endif

#endif
