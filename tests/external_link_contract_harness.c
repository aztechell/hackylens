#include "capability_fake_external_link.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(condition))                                                  \
        {                                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n",             \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static const hk_owner_t OWNER_A = {1U, 7U};
static const hk_owner_t OWNER_B = {2U, 3U};

static uint8_t cancel_probe(const void *context)
{
    return *(const uint8_t *)context;
}

static hk_external_link_uart_config_t uart_config(uint32_t baud)
{
    hk_external_link_uart_config_t config = {
        sizeof(config), HK_EXTERNAL_LINK_UART_CONFIG_VERSION, baud, 0U};
    return config;
}

static hk_external_link_i2c_controller_config_t controller_config(
    uint32_t frequency_hz)
{
    hk_external_link_i2c_controller_config_t config = {
        sizeof(config),
        HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION,
        frequency_hz,
        0U};
    return config;
}

static hk_external_link_i2c_target_config_t target_config(uint16_t address)
{
    hk_external_link_i2c_target_config_t config = {
        sizeof(config),
        HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION,
        address,
        0U,
        0U};
    return config;
}

static int case_identity_modes_and_exclusivity(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t first = {0};
    hk_external_link_t second = {0};
    hk_external_link_t stale;
    hk_external_link_info_t info;
    hk_external_link_uart_config_t uart = uart_config(115200U);
    hk_external_link_i2c_controller_config_t controller =
        controller_config(400000U);
    hk_external_link_i2c_target_config_t target = target_config(0x32U);
    uint32_t mode = 99U;

    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURES_0_1);
    CHECK(hk_external_link_acquire(
              OWNER_A,
              &request,
              HK_EXTERNAL_LINK_FEATURE_UART |
                  HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER,
              &first) == HK_OK);
    CHECK(hk_external_link_get_info(OWNER_A, &first, &info) == HK_OK);
    CHECK(info.features == HK_EXTERNAL_LINK_FEATURES_0_1);
    CHECK(info.maximum_poll_bytes == HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES);
    CHECK(hk_external_link_acquire(
              OWNER_B, &request, HK_EXTERNAL_LINK_FEATURE_UART, &second) ==
          HK_ERR_BUSY);
    CHECK(hk_external_link_get_mode(OWNER_B, &first, &mode) ==
          HK_ERR_WRONG_OWNER);
    CHECK(hk_external_link_configure_i2c_target(
              OWNER_A, &first, &target) == HK_ERR_NOT_DECLARED);
    uart.baud = 1U;
    CHECK(hk_external_link_configure_uart(OWNER_A, &first, &uart) ==
          HK_ERR_INVALID_ARGUMENT);
    uart.baud = 115200U;
    CHECK(hk_external_link_configure_uart(OWNER_A, &first, &uart) == HK_OK);
    CHECK(hk_external_link_get_mode(OWNER_A, &first, &mode) == HK_OK);
    CHECK(mode == HK_EXTERNAL_LINK_MODE_UART);
    CHECK(hk_external_link_configure_i2c_controller(
              OWNER_A, &first, &controller) == HK_OK);
    CHECK(hk_fake_external_link_metrics()->route_changes == 2U);
    CHECK(hk_fake_external_link_metrics()->peripheral_resets == 1U);
    stale = first;
    CHECK(hk_external_link_release(
              OWNER_A, (hk_deadline_t){1000U}, &first) == HK_OK);
    CHECK(hk_lease_is_zero(&first.lease));
    CHECK(hk_external_link_get_mode(OWNER_A, &stale, &mode) ==
          HK_ERR_STALE_HANDLE);

    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURE_UART);
    memset(&first, 0, sizeof(first));
    CHECK(hk_external_link_acquire(
              OWNER_A,
              &request,
              HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER,
              &first) == HK_ERR_FEATURE_UNAVAILABLE);
    return 0;
}

static int case_uart_partial_progress_and_drain(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t handle = {0};
    hk_external_link_uart_config_t config = uart_config(115200U);
    hk_external_link_op_t operation = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_t old_operation;
    hk_external_link_op_progress_t progress;
    uint8_t tx_bytes[70];
    uint8_t one = 0x5aU;
    uint8_t incoming[40];
    uint8_t rx_bytes[64] = {0};
    hk_buffer_view_t tx = {
        tx_bytes, sizeof(tx_bytes), 0U, HK_BUFFER_ACCESS_READABLE};
    hk_buffer_view_t tiny_tx = {
        &one, 1U, 0U, HK_BUFFER_ACCESS_READABLE};
    hk_buffer_view_t rx = {
        rx_bytes, sizeof(rx_bytes), 0U, HK_BUFFER_ACCESS_WRITABLE};
    uint32_t received = 99U;
    uint32_t index;

    for (index = 0U; index < sizeof(tx_bytes); ++index)
        tx_bytes[index] = (uint8_t)index;
    for (index = 0U; index < sizeof(incoming); ++index)
        incoming[index] = (uint8_t)(0xa0U + index);

    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURE_UART);
    hk_fake_external_link_set_now_us(100U);
    CHECK(hk_external_link_acquire(
              OWNER_A, &request, HK_EXTERNAL_LINK_FEATURE_UART, &handle) ==
          HK_OK);
    CHECK(hk_external_link_configure_uart(OWNER_A, &handle, &config) ==
          HK_OK);
    CHECK(hk_external_link_uart_read(
              OWNER_A, &handle, &rx, &received) == HK_OK);
    CHECK(received == 0U);
    CHECK(hk_fake_external_link_feed_uart_rx(
              incoming, sizeof(incoming)) == HK_OK);
    CHECK(hk_external_link_uart_read(
              OWNER_A, &handle, &rx, &received) == HK_OK);
    CHECK(received == HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES);
    CHECK(memcmp(rx_bytes, incoming, received) == 0);
    CHECK(hk_external_link_uart_read(
              OWNER_A, &handle, &rx, &received) == HK_OK);
    CHECK(received == 8U);

    hk_fake_external_link_set_uart_drain_polls(2U);
    CHECK(hk_external_link_uart_write_begin(
              OWNER_A,
              &handle,
              &tx,
              (hk_deadline_t){1000U},
              NULL,
              &operation) == HK_PENDING);
    CHECK(hk_fake_external_link_metrics()->borrowed_tx_bytes == 70U);
    CHECK(hk_fake_external_link_metrics()->original_deadline.at_us == 1000U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    CHECK(progress.tx_completed_bytes == 32U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    CHECK(progress.tx_completed_bytes == 64U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    CHECK(progress.tx_completed_bytes == 70U);
    CHECK((progress.flags & HK_EXTERNAL_LINK_PROGRESS_UART_DRAINING) != 0U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_OK);
    CHECK(progress.terminal_result == HK_OK);
    CHECK((progress.flags & HK_EXTERNAL_LINK_PROGRESS_TERMINAL) != 0U);
    CHECK(hk_fake_external_link_metrics()->uart_tx_bytes == 70U);
    CHECK(hk_fake_external_link_metrics()->borrowed_tx_bytes == 0U);

    old_operation = operation;
    hk_fake_external_link_set_uart_drain_polls(0U);
    CHECK(hk_external_link_uart_write_begin(
              OWNER_A,
              &handle,
              &tiny_tx,
              (hk_deadline_t){1000U},
              NULL,
              &operation) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &old_operation, &progress) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_external_link_cancel(
              OWNER_A, &handle, &operation, &progress) == HK_ERR_CANCELLED);
    CHECK(hk_external_link_release(
              OWNER_A, (hk_deadline_t){1000U}, &handle) == HK_OK);
    return 0;
}

static int case_cancel_and_no_late_effects(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t handle = {0};
    hk_external_link_uart_config_t uart = uart_config(115200U);
    hk_external_link_i2c_controller_config_t controller =
        controller_config(100000U);
    hk_external_link_op_t operation = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_progress_t progress;
    uint8_t cancelled = 1U;
    hk_cancel_t cancel = {cancel_probe, &cancelled};
    uint8_t bytes[70] = {0};
    hk_buffer_view_t tx = {
        bytes, sizeof(bytes), 0U, HK_BUFFER_ACCESS_READABLE};
    uint32_t effects_at_cancel;
    uint32_t routes_before;

    hk_fake_external_link_reset(
        HK_EXTERNAL_LINK_FEATURE_UART |
        HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER);
    CHECK(hk_external_link_acquire(
              OWNER_A,
              &request,
              HK_EXTERNAL_LINK_FEATURE_UART |
                  HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER,
              &handle) == HK_OK);
    CHECK(hk_external_link_configure_uart(OWNER_A, &handle, &uart) == HK_OK);
    CHECK(hk_external_link_uart_write_begin(
              OWNER_A,
              &handle,
              &tx,
              (hk_deadline_t){500U},
              &cancel,
              &operation) == HK_ERR_CANCELLED);
    CHECK(operation.slot == 0U && operation.generation == 0U);
    CHECK(hk_fake_external_link_metrics()->uart_tx_bytes == 0U);

    cancelled = 0U;
    CHECK(hk_external_link_uart_write_begin(
              OWNER_A,
              &handle,
              &tx,
              (hk_deadline_t){500U},
              &cancel,
              &operation) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    CHECK(progress.tx_completed_bytes == 32U);
    routes_before = hk_fake_external_link_metrics()->route_changes;
    CHECK(hk_external_link_configure_i2c_controller(
              OWNER_A, &handle, &controller) == HK_ERR_BUSY);
    CHECK(hk_fake_external_link_metrics()->route_changes == routes_before);
    cancelled = 1U;
    hk_fake_external_link_set_now_us(500U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_ERR_CANCELLED);
    effects_at_cancel = hk_fake_external_link_metrics()->uart_tx_bytes;
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_ERR_CANCELLED);
    CHECK(hk_fake_external_link_metrics()->uart_tx_bytes == effects_at_cancel);
    CHECK(hk_fake_external_link_metrics()->late_effect_attempts == 0U);
    CHECK(hk_fake_external_link_metrics()->borrowed_tx_bytes == 0U);
    CHECK(hk_external_link_configure_i2c_controller(
              OWNER_A, &handle, &controller) == HK_OK);
    CHECK(hk_external_link_release(
              OWNER_A, (hk_deadline_t){1000U}, &handle) == HK_OK);
    return 0;
}

static int case_i2c_whole_transaction_semantics(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t handle = {0};
    hk_external_link_i2c_controller_config_t config =
        controller_config(400000U);
    hk_external_link_op_t operation = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_progress_t progress;
    uint8_t tx_bytes[20] = {0};
    uint8_t immediate_bytes[40] = {0};
    uint8_t rx_source[20];
    uint8_t rx_bytes[20] = {0};
    hk_external_link_i2c_transfer_t transfer = {
        sizeof(transfer),
        HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION,
        0x50U,
        0U,
        {tx_bytes, sizeof(tx_bytes), 0U, HK_BUFFER_ACCESS_READABLE},
        {rx_bytes, sizeof(rx_bytes), 0U, HK_BUFFER_ACCESS_WRITABLE},
        0U};
    hk_external_link_i2c_transfer_t write_only;
    hk_external_link_i2c_transfer_t immediate_transfer;
    uint32_t resets;
    uint32_t index;

    for (index = 0U; index < sizeof(rx_source); ++index)
        rx_source[index] = (uint8_t)(0x30U + index);
    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER);
    hk_fake_external_link_set_now_us(100U);
    CHECK(hk_external_link_acquire(
              OWNER_A,
              &request,
              HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER,
              &handle) == HK_OK);
    CHECK(hk_external_link_configure_i2c_controller(
              OWNER_A, &handle, &config) == HK_OK);
    CHECK(hk_fake_external_link_set_i2c_rx(
              rx_source, sizeof(rx_source)) == HK_OK);
    CHECK(hk_external_link_i2c_transfer_begin(
              OWNER_A,
              &handle,
              &transfer,
              (hk_deadline_t){1000U},
              NULL,
              &operation) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    CHECK(progress.tx_completed_bytes == 20U);
    CHECK(progress.rx_completed_bytes == 12U);
    CHECK((progress.flags & HK_EXTERNAL_LINK_PROGRESS_RX_PREFIX_READABLE) !=
          0U);
    CHECK(memcmp(rx_bytes, rx_source, 12U) == 0);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_OK);
    CHECK(progress.rx_completed_bytes == 20U);
    CHECK(memcmp(rx_bytes, rx_source, sizeof(rx_bytes)) == 0);
    CHECK(hk_fake_external_link_metrics()->original_deadline.at_us == 1000U);

    write_only = transfer;
    write_only.tx.size_bytes = 20U;
    write_only.rx.data = NULL;
    write_only.rx.size_bytes = 0U;
    write_only.rx.flags = HK_BUFFER_ACCESS_WRITABLE;
    hk_fake_external_link_fail_next_i2c(HK_ERR_IO, 8U);
    resets = hk_fake_external_link_metrics()->peripheral_resets;
    CHECK(hk_external_link_i2c_transfer_begin(
              OWNER_A,
              &handle,
              &write_only,
              (hk_deadline_t){1000U},
              NULL,
              &operation) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_ERR_IO);
    CHECK(progress.tx_completed_bytes == 8U);
    CHECK(hk_fake_external_link_metrics()->peripheral_resets == resets + 1U);

    CHECK(hk_external_link_i2c_transfer_begin(
              OWNER_A,
              &handle,
              &write_only,
              (hk_deadline_t){150U},
              NULL,
              &operation) == HK_PENDING);
    hk_fake_external_link_set_now_us(150U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(progress.tx_completed_bytes == 0U);
    CHECK(hk_fake_external_link_metrics()->original_deadline.at_us == 150U);

    immediate_transfer = write_only;
    immediate_transfer.tx.data = immediate_bytes;
    immediate_transfer.tx.size_bytes = sizeof(immediate_bytes);
    CHECK(hk_external_link_i2c_transfer_begin(
              OWNER_A,
              &handle,
              &immediate_transfer,
              HK_DEADLINE_IMMEDIATE,
              NULL,
              &operation) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(progress.tx_completed_bytes == 32U);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(progress.tx_completed_bytes == 32U);
    CHECK(hk_external_link_release(
              OWNER_A, (hk_deadline_t){1000U}, &handle) == HK_OK);
    return 0;
}

static int case_i2c_target_buffers(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t handle = {0};
    hk_external_link_i2c_target_config_t config = target_config(0x32U);
    hk_external_link_target_event_t event;
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    uint8_t receive[4] = {0};
    uint8_t response[3] = {9U, 8U, 7U};
    hk_buffer_view_t short_rx = {
        receive, 2U, 0U, HK_BUFFER_ACCESS_WRITABLE};
    hk_buffer_view_t rx = {
        receive, sizeof(receive), 0U, HK_BUFFER_ACCESS_WRITABLE};
    hk_buffer_view_t tx = {
        response, sizeof(response), 0U, HK_BUFFER_ACCESS_READABLE};
    const uint8_t *stored_response;
    uint32_t response_size = 0U;

    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURE_I2C_TARGET);
    CHECK(hk_external_link_acquire(
              OWNER_A,
              &request,
              HK_EXTERNAL_LINK_FEATURE_I2C_TARGET,
              &handle) == HK_OK);
    CHECK(hk_external_link_configure_i2c_target(
              OWNER_A, &handle, &config) == HK_OK);
    CHECK(hk_external_link_i2c_target_poll(
              OWNER_A, &handle, &rx, &event) == HK_PENDING);
    CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_NONE);
    CHECK(hk_fake_external_link_push_target_event(
              HK_EXTERNAL_LINK_TARGET_EVENT_WRITE,
              payload,
              sizeof(payload),
              0U) == HK_OK);
    CHECK(hk_external_link_i2c_target_poll(
              OWNER_A, &handle, &short_rx, &event) == HK_ERR_LIMIT);
    CHECK(hk_external_link_i2c_target_poll(
              OWNER_A, &handle, &rx, &event) == HK_OK);
    CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE);
    CHECK(event.received_bytes == sizeof(payload));
    CHECK(memcmp(receive, payload, sizeof(payload)) == 0);
    CHECK(hk_external_link_i2c_target_respond(
              OWNER_A, &handle, &tx) == HK_OK);
    response[0] = 0U;
    stored_response = hk_fake_external_link_target_response(&response_size);
    CHECK(response_size == 3U);
    CHECK(stored_response[0] == 9U);
    CHECK(hk_external_link_release(
              OWNER_A, (hk_deadline_t){1000U}, &handle) == HK_OK);
    return 0;
}

static int case_release_quiesces_active_operation(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t handle = {0};
    hk_external_link_t stale;
    hk_external_link_uart_config_t config = uart_config(115200U);
    hk_external_link_op_t operation = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_progress_t progress;
    uint8_t bytes[70] = {0};
    hk_buffer_view_t tx = {
        bytes, sizeof(bytes), 0U, HK_BUFFER_ACCESS_READABLE};
    uint32_t effects;

    hk_fake_external_link_reset(HK_EXTERNAL_LINK_FEATURE_UART);
    CHECK(hk_external_link_acquire(
              OWNER_A, &request, HK_EXTERNAL_LINK_FEATURE_UART, &handle) ==
          HK_OK);
    CHECK(hk_external_link_configure_uart(OWNER_A, &handle, &config) ==
          HK_OK);
    CHECK(hk_external_link_uart_write_begin(
              OWNER_A,
              &handle,
              &tx,
              (hk_deadline_t){1000U},
              NULL,
              &operation) == HK_PENDING);
    CHECK(hk_external_link_poll(
              OWNER_A, &handle, &operation, &progress) == HK_PENDING);
    effects = hk_fake_external_link_metrics()->uart_tx_bytes;
    stale = handle;
    CHECK(hk_external_link_release(
              OWNER_A, (hk_deadline_t){1000U}, &handle) == HK_OK);
    CHECK(hk_fake_external_link_metrics()->active_operations == 0U);
    CHECK(hk_fake_external_link_metrics()->borrowed_tx_bytes == 0U);
    CHECK(hk_external_link_poll(
              OWNER_A, &stale, &operation, &progress) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_fake_external_link_metrics()->uart_tx_bytes == effects);
    CHECK(hk_fake_external_link_metrics()->late_effect_attempts == 0U);
    return 0;
}

int main(void)
{
    CHECK(case_identity_modes_and_exclusivity() == 0);
    CHECK(case_uart_partial_progress_and_drain() == 0);
    CHECK(case_cancel_and_no_late_effects() == 0);
    CHECK(case_i2c_whole_transaction_semantics() == 0);
    CHECK(case_i2c_target_buffers() == 0);
    CHECK(case_release_quiesces_active_operation() == 0);
    puts("EXTERNAL_LINK_CONTRACT_OK cases=6 burst=32 fixed_capacity=256");
    return 0;
}
