#include "hk_config.h"

#if HK_ENABLE_APP_KIBBLE_LM
#include <string.h>

#include "app_services.h"
#include "../services/kibble_session.h"

static const char s_app_owner;

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

hk_result_t hk_app_kibble_open_step(const hk_app_context_t *ctx, const char **status)
{
    hk_result_t result = authorized(ctx, 1U);
    if(result != HK_OK || !status)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    return kibble_session_open_step(&s_app_owner, status);
}

hk_result_t hk_app_kibble_start(const hk_app_context_t *ctx, const char *prompt, uint32_t seed)
{
    hk_result_t result = authorized(ctx, 1U);
    return result == HK_OK ? kibble_session_start(&s_app_owner, prompt, seed) : result;
}

hk_result_t hk_app_kibble_poll(const hk_app_context_t *ctx, hk_kibble_token_t *token)
{
    hk_result_t result = authorized(ctx, 1U);
    if(result != HK_OK || !token)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    return kibble_session_poll(&s_app_owner, token);
}

hk_result_t hk_app_kibble_close(const hk_app_context_t *ctx, hk_deadline_t deadline)
{
    hk_result_t result = authorized(ctx, 0U);
    if(result == HK_OK)
        kibble_session_close(&s_app_owner, deadline);
    return result;
}
#endif
