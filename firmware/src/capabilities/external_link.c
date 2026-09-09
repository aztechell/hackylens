#include <hackylens/capability/external_link.h>

#include <limits.h>
#include <stddef.h>

#include "external_link_provider.h"

const hk_external_link_service_t *hk_external_link_service(void)
{
    return hk_external_link_binding.get_info ? &hk_external_link_binding : NULL;
}
static hk_result_t quarantine_internal(const hk_external_link_t *handle, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL && handle && handle->service &&
       handle->service->state && handle->service->state->claimant == handle)
        handle->service->state->quarantined = 1U;
    return result;
}
static hk_result_t provider_for(const hk_external_link_t *handle,
    const hk_external_link_service_t **provider)
{
    if(!handle || !provider) return HK_ERR_INVALID_ARGUMENT;
    if(!handle->service || !handle->service->state ||
       handle->service->state->claimant != handle) return HK_ERR_STALE_HANDLE;
    if(handle->service->state->quarantined) return HK_ERR_INTERNAL;
    *provider = handle->service;
    return HK_OK;
}

static hk_result_t validate_view(
    const hk_buffer_view_t *view, uint32_t access, uint32_t maximum,
    uint8_t allow_empty)
{
    if(!view || view->stride_bytes != 0U || view->flags != access ||
       (!view->data && view->size_bytes != 0U) ||
       (!allow_empty && view->size_bytes == 0U))
        return HK_ERR_INVALID_ARGUMENT;
    if(view->size_bytes > maximum)
        return HK_ERR_LIMIT;
    return HK_OK;
}

static hk_result_t validate_struct(
    uint16_t size, uint16_t expected_size,
    uint16_t version, uint16_t expected_version)
{
    if(size < expected_size)
        return HK_ERR_INVALID_ARGUMENT;
    if(version != expected_version)
        return HK_ERR_VERSION_INCOMPATIBLE;
    return HK_OK;
}

hk_result_t hk_external_link_open(const hk_external_link_service_t *service,
    uint64_t mode_features, hk_external_link_t *handle)
{
    hk_result_t result;
    if(!handle || !mode_features || (mode_features & ~HK_EXTERNAL_LINK_FEATURES_0_1))
        return HK_ERR_INVALID_ARGUMENT;
    if(!service) return HK_ERR_CAPABILITY_ABSENT;
    if(!service->state || !service->open || !service->close || !service->retire ||
       !service->get_info || !service->get_mode || !service->configure_uart ||
       !service->configure_i2c_controller || !service->configure_i2c_target ||
       !service->uart_write_begin || !service->uart_read || !service->i2c_transfer_begin ||
       !service->poll || !service->cancel || !service->target_poll || !service->target_preload ||
       service->reserved) return HK_ERR_INTERNAL;
    if(service->state->quarantined) return HK_ERR_INTERNAL;
    if(service->state->claimant) return HK_ERR_BUSY;
    *handle = (hk_external_link_t){service, mode_features};
    result = service->open(service->context, handle, mode_features);
    if(result != HK_OK) { *handle = (hk_external_link_t){0}; return result; }
    service->state->claimant = handle;
    return HK_OK;
}
hk_result_t hk_external_link_close(hk_external_link_t *handle, hk_deadline_t deadline)
{
    const hk_external_link_service_t *service;
    hk_result_t result;
    if(!handle || deadline.at_us == UINT64_MAX) return HK_ERR_INVALID_ARGUMENT;
    if(!handle->service && !handle->mode_features) return HK_OK;
    result = provider_for(handle, &service);
    if(result != HK_OK) return result;
    result = service->close(service->context, handle, deadline);
    if(result != HK_OK) return quarantine_internal(handle, result);
    service->state->claimant = NULL;
    *handle = (hk_external_link_t){0};
    return HK_OK;
}
hk_result_t hk_external_link_retire(hk_external_link_t *handle, hk_deadline_t deadline)
{
    const hk_external_link_service_t *service;
    hk_result_t result;
    if(!handle) return HK_ERR_INVALID_ARGUMENT;
    if(!handle->service && !handle->mode_features)
        return deadline.at_us == UINT64_MAX ? HK_ERR_INVALID_ARGUMENT : HK_OK;
    service = handle->service;
    if(!service || !service->state || service->state->claimant != handle) {
        *handle = (hk_external_link_t){0}; return HK_ERR_STALE_HANDLE;
    }
    result = deadline.at_us == UINT64_MAX ? HK_ERR_INVALID_ARGUMENT :
        service->close(service->context, handle, deadline);
    /* Bounded hardware quiesce and borrow invalidation are mandatory even
       after expiry. No peripheral callback may retain caller storage. */
    service->retire(service->context, handle);
    if(result != HK_OK) service->state->quarantined = 1U;
    service->state->claimant = NULL;
    *handle = (hk_external_link_t){0};
    return result;
}

#define EXTERNAL_CALL(name, argument)                                         \
    do {                                                                       \
        const hk_external_link_service_t *provider;                                 \
        hk_result_t result = provider_for(handle, &provider);           \
        if(result == HK_OK)                                                    \
            result = provider->name(                                           \
                provider->context, handle, argument);                  \
        return quarantine_internal(handle, result);                     \
    } while(0)

hk_result_t hk_external_link_get_info(
    const hk_external_link_t *handle,
    hk_external_link_info_t *info)
{
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(get_info, info);
}

hk_result_t hk_external_link_get_mode(
    const hk_external_link_t *handle, uint32_t *mode)
{
    if(!mode)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(get_mode, mode);
}

hk_result_t hk_external_link_configure_uart(
    const hk_external_link_t *handle,
    const hk_external_link_uart_config_t *config)
{
    hk_result_t result;

    if(!config)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_struct(
        config->struct_size, sizeof(*config), config->struct_version,
        HK_EXTERNAL_LINK_UART_CONFIG_VERSION);
    if(result != HK_OK)
        return result;
    if(config->reserved != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(configure_uart, config);
}

hk_result_t hk_external_link_configure_i2c_controller(
    const hk_external_link_t *handle,
    const hk_external_link_i2c_controller_config_t *config)
{
    hk_result_t result;

    if(!config)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_struct(
        config->struct_size, sizeof(*config), config->struct_version,
        HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION);
    if(result != HK_OK)
        return result;
    if(config->reserved != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(configure_i2c_controller, config);
}

hk_result_t hk_external_link_configure_i2c_target(
    const hk_external_link_t *handle,
    const hk_external_link_i2c_target_config_t *config)
{
    hk_result_t result;

    if(!config)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_struct(
        config->struct_size, sizeof(*config), config->struct_version,
        HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION);
    if(result != HK_OK)
        return result;
    if(config->reserved0 != 0U || config->reserved1 != 0U ||
       config->address > UINT16_C(0x7f))
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(configure_i2c_target, config);
}

hk_result_t hk_external_link_uart_write_begin(
    const hk_external_link_t *handle,
    const hk_buffer_view_t *tx, hk_deadline_t deadline,
    const hk_cancel_t *cancel, hk_external_link_op_t *operation)
{
    const hk_external_link_service_t *provider;
    hk_result_t result;

    if(!operation || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    *operation = HK_EXTERNAL_LINK_OP_NONE;
    result = validate_view(tx, HK_BUFFER_ACCESS_READABLE, 256U, 0U);
    if(result != HK_OK)
        return result;
    result = provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->uart_write_begin(
            provider->context, handle, tx, deadline, cancel,
            operation);
    return quarantine_internal(handle, result);
}

hk_result_t hk_external_link_uart_read(
    const hk_external_link_t *handle,
    hk_buffer_view_t *rx, uint32_t *received_bytes)
{
    const hk_external_link_service_t *provider;
    hk_result_t result;

    if(!received_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    *received_bytes = 0U;
    result = validate_view(rx, HK_BUFFER_ACCESS_WRITABLE, 256U, 1U);
    if(result != HK_OK)
        return result;
    result = provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->uart_read(
            provider->context, handle, rx, received_bytes);
    return quarantine_internal(handle, result);
}

hk_result_t hk_external_link_i2c_transfer_begin(
    const hk_external_link_t *handle,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    const hk_external_link_service_t *provider;
    hk_result_t result;

    if(!transfer || !operation || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    *operation = HK_EXTERNAL_LINK_OP_NONE;
    result = validate_struct(
        transfer->struct_size, sizeof(*transfer), transfer->struct_version,
        HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION);
    if(result != HK_OK)
        return result;
    if(transfer->reserved0 != 0U || transfer->reserved1 != 0U ||
       transfer->address > UINT16_C(0x7f))
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_view(
        &transfer->tx, HK_BUFFER_ACCESS_READABLE, 256U, 1U);
    if(result == HK_OK)
        result = validate_view(
            &transfer->rx, HK_BUFFER_ACCESS_WRITABLE, 256U, 1U);
    if(result != HK_OK)
        return result;
    if(transfer->tx.size_bytes == 0U && transfer->rx.size_bytes == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    result = provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->i2c_transfer_begin(
            provider->context, handle, transfer, deadline, cancel,
            operation);
    return quarantine_internal(handle, result);
}

static hk_result_t operation_call(
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress, uint8_t cancel)
{
    const hk_external_link_service_t *provider;
    hk_result_t result;

    if(!operation || !progress)
        return HK_ERR_INVALID_ARGUMENT;
    result = provider_for(handle, &provider);
    if(result == HK_OK)
        result = (cancel ? provider->cancel : provider->poll)(
            provider->context, handle, operation, progress);
    return quarantine_internal(handle, result);
}

hk_result_t hk_external_link_poll(
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    return operation_call(handle, operation, progress, 0U);
}

hk_result_t hk_external_link_cancel(
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    return operation_call(handle, operation, progress, 1U);
}

hk_result_t hk_external_link_i2c_target_poll(
    const hk_external_link_t *handle,
    hk_buffer_view_t *rx, hk_external_link_target_event_t *event)
{
    const hk_external_link_service_t *provider;
    hk_result_t result;

    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_view(rx, HK_BUFFER_ACCESS_WRITABLE, 256U, 1U);
    if(result != HK_OK)
        return result;
    result = provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->target_poll(
            provider->context, handle, rx, event);
    return quarantine_internal(handle, result);
}

hk_result_t hk_external_link_i2c_target_preload_response(
    const hk_external_link_t *handle,
    const hk_buffer_view_t *tx)
{
    const hk_external_link_service_t *provider;
    hk_result_t result = validate_view(
        tx, HK_BUFFER_ACCESS_READABLE, 256U, 1U);

    if(result != HK_OK)
        return result;
    result = provider_for(handle, &provider);
    if(result == HK_OK)
        result = provider->target_preload(
            provider->context, handle, tx);
    return quarantine_internal(handle, result);
}
