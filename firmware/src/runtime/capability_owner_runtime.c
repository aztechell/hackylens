#include "capability_owner_runtime.h"

#include <stddef.h>

#include "../capabilities/capability_inventory_binding.h"
#include "../capabilities/capability_provider.h"
#include "../core/hk_app.h"

static hk_capability_core_t s_capability_core;
static const hk_app_t *s_current_app;
static hk_owner_t s_current_owner;
static uint8_t s_initialized;

static hk_result_t ensure_initialized(void)
{
    const hk_capability_info_t *inventory;
    const hk_capability_provider_t *const *providers;
    uint16_t provider_count;
    hk_result_t result;

    if(s_initialized)
        return HK_OK;
    hk_generated_capability_inventory_get(
        &inventory, &providers, &provider_count);
    result = hk_capability_core_init(
        &s_capability_core, inventory, providers, provider_count);
    if(result == HK_OK)
        s_initialized = 1U;
    return result;
}

hk_result_t capability_owner_runtime_enter(const hk_app_t *app)
{
    const hk_capability_grant_t *grants;
    uint16_t grant_count;
    hk_result_t result;

    if(!app)
        return HK_ERR_INVALID_ARGUMENT;
    result = ensure_initialized();
    if(result != HK_OK)
        return result;
    if(!hk_owner_is_zero(s_current_owner))
    {
        result = hk_capability_core_owner_close(
            &s_capability_core, s_current_owner, 0U, HK_DEADLINE_IMMEDIATE);
        s_current_owner = HK_OWNER_NONE;
        s_current_app = NULL;
        if(result != HK_OK)
            return result;
    }
    grants = hk_generated_capability_grants_for(app->id, &grant_count);
    result = hk_capability_core_owner_open(
        &s_capability_core, grants, grant_count, &s_current_owner);
    if(result == HK_OK)
        s_current_app = app;
    return result;
}

hk_result_t capability_owner_runtime_exit(const hk_app_t *app)
{
    hk_result_t result;

    result = ensure_initialized();
    if(result != HK_OK)
        return result;
    if(hk_owner_is_zero(s_current_owner))
        return HK_OK;
    if(app != s_current_app)
        return HK_ERR_WRONG_OWNER;
    result = hk_capability_core_owner_close(
        &s_capability_core, s_current_owner, 0U, HK_DEADLINE_IMMEDIATE);
    s_current_owner = HK_OWNER_NONE;
    s_current_app = NULL;
    return result;
}

hk_owner_t capability_owner_runtime_current(const hk_app_t *app)
{
    if(app != s_current_app)
        return HK_OWNER_NONE;
    return s_current_owner;
}

hk_result_t hk_capability_inventory_get(
    const hk_capability_info_t **entries,
    uint16_t *count)
{
    hk_result_t result;

    if(!entries || !count)
        return HK_ERR_INVALID_ARGUMENT;
    result = ensure_initialized();
    if(result != HK_OK)
        return result;
    *entries = hk_capability_core_inventory(&s_capability_core, count);
    return HK_OK;
}
