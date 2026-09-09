#ifndef HK_EXTERNAL_LINK_PROVIDER_H
#define HK_EXTERNAL_LINK_PROVIDER_H

#include <hackylens/capability/external_link.h>

typedef hk_result_t (*hk_external_link_provider_open_fn)(
    void *context, const hk_external_link_t *session, uint64_t mode_features);
typedef hk_result_t (*hk_external_link_provider_close_fn)(
    void *context, const hk_external_link_t *session, hk_deadline_t deadline);
typedef hk_result_t (*hk_external_link_provider_info_fn)(
    void *context, const hk_external_link_t *session, hk_external_link_info_t *info);
typedef hk_result_t (*hk_external_link_provider_mode_fn)(
    void *context, const hk_external_link_t *session, uint32_t *mode);
typedef hk_result_t (*hk_external_link_provider_uart_configure_fn)(
    void *context, const hk_external_link_t *session,
    const hk_external_link_uart_config_t *config);
typedef hk_result_t (*hk_external_link_provider_i2c_controller_configure_fn)(
    void *context, const hk_external_link_t *session,
    const hk_external_link_i2c_controller_config_t *config);
typedef hk_result_t (*hk_external_link_provider_i2c_target_configure_fn)(
    void *context, const hk_external_link_t *session,
    const hk_external_link_i2c_target_config_t *config);
typedef hk_result_t (*hk_external_link_provider_uart_write_begin_fn)(
    void *context, const hk_external_link_t *session, const hk_buffer_view_t *tx,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation);
typedef hk_result_t (*hk_external_link_provider_uart_read_fn)(
    void *context, const hk_external_link_t *session, hk_buffer_view_t *rx,
    uint32_t *received_bytes);
typedef hk_result_t (*hk_external_link_provider_i2c_transfer_begin_fn)(
    void *context, const hk_external_link_t *session,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation);
typedef hk_result_t (*hk_external_link_provider_operation_fn)(
    void *context, const hk_external_link_t *session,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress);
typedef hk_result_t (*hk_external_link_provider_target_poll_fn)(
    void *context, const hk_external_link_t *session, hk_buffer_view_t *rx,
    hk_external_link_target_event_t *event);
typedef hk_result_t (*hk_external_link_provider_target_preload_fn)(
    void *context, const hk_external_link_t *session, const hk_buffer_view_t *tx);

typedef struct {
    const hk_external_link_t *claimant;
    uint8_t quarantined;
} hk_external_link_state_t;

struct hk_external_link_service
{
    hk_external_link_state_t *state;
    void *context;
    void (*retire)(void *context, const hk_external_link_t *session);
    hk_external_link_provider_open_fn open;
    hk_external_link_provider_close_fn close;
    hk_external_link_provider_info_fn get_info;
    hk_external_link_provider_mode_fn get_mode;
    hk_external_link_provider_uart_configure_fn configure_uart;
    hk_external_link_provider_i2c_controller_configure_fn
        configure_i2c_controller;
    hk_external_link_provider_i2c_target_configure_fn configure_i2c_target;
    hk_external_link_provider_uart_write_begin_fn uart_write_begin;
    hk_external_link_provider_uart_read_fn uart_read;
    hk_external_link_provider_i2c_transfer_begin_fn i2c_transfer_begin;
    hk_external_link_provider_operation_fn poll;
    hk_external_link_provider_operation_fn cancel;
    hk_external_link_provider_target_poll_fn target_poll;
    hk_external_link_provider_target_preload_fn target_preload;
    uint32_t reserved;
};
extern const hk_external_link_service_t hk_external_link_binding;

#endif
