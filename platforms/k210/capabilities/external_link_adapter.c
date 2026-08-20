#include "../../../firmware/src/capabilities/capability_provider.h"
#include "../../../firmware/src/capabilities/external_link_provider.h"

#include <hackylens/capability/external_link.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "../hal/hal_external_link.h"
#include "../hal/hal_time.h"

#define K210_EXTERNAL_MAX_BYTES 256U
#define K210_EXTERNAL_POLL_BYTES 32U
#define K210_EXTERNAL_UART_MIN_BAUD 1200U
#define K210_EXTERNAL_UART_MAX_BAUD 2000000U
#define K210_EXTERNAL_I2C_MIN_HZ 10000U
#define K210_EXTERNAL_I2C_MAX_HZ 1000000U
#define K210_EXTERNAL_OPERATION_SLOT 1U

typedef enum
{
    K210_EXTERNAL_OP_NONE = 0,
    K210_EXTERNAL_OP_IN_FLIGHT,
    K210_EXTERNAL_OP_TERMINAL,
} k210_external_operation_state_t;

typedef struct
{
    k210_external_operation_state_t state;
    uint32_t generation;
    uint32_t kind;
    hk_deadline_t deadline;
    hk_cancel_t cancel;
    const uint8_t *tx;
    uint8_t *rx;
    uint32_t tx_size;
    uint32_t rx_size;
    uint32_t tx_done;
    uint32_t rx_done;
    uint32_t read_commands;
    uint16_t i2c_address;
    hk_result_t terminal_result;
    uint8_t controller_started;
    uint8_t terminal_observed;
} k210_external_operation_t;

typedef struct
{
    hk_lease_t lease;
    uint64_t mode_features;
    uint32_t mode;
    uint32_t uart_baud;
    uint32_t i2c_frequency_hz;
    uint32_t next_operation_generation;
    k210_external_operation_t operation;
    volatile uint32_t target_event_type;
    volatile uint32_t target_received_bytes;
    volatile uint32_t target_requested_bytes;
    volatile uint8_t target_event_pending;
    volatile uint8_t target_rx_overflow;
    volatile uint16_t target_rx_size;
    uint8_t target_buffer[2][K210_EXTERNAL_MAX_BYTES];
    volatile uint16_t target_response_size[2];
    volatile uint32_t target_read_index;
    volatile uint16_t target_read_size;
    volatile uint8_t target_preload_slot;
    volatile uint8_t target_active_slot;
    volatile uint8_t target_rx_slot;
    volatile uint8_t target_preload_active;
    volatile uint8_t target_read_active;
    uint8_t active;
} k210_external_state_t;

static k210_external_state_t s_external;

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t lease_equal(const hk_lease_t *left, const hk_lease_t *right)
{
    return (uint8_t)(left && right && left->slot == right->slot &&
                     left->generation == right->generation &&
                     left->capability_id == right->capability_id &&
                     owner_equal(left->owner, right->owner));
}

static uint8_t deadline_expired(hk_deadline_t deadline)
{
    return (uint8_t)(deadline.at_us != 0U &&
                     hal_time_us() >= deadline.at_us);
}

static uint8_t cancellation_requested(const hk_cancel_t *cancel)
{
    return (uint8_t)(cancel && cancel->probe &&
                     cancel->probe(cancel->context));
}

static void clear_target(k210_external_state_t *state)
{
    state->target_event_type = HK_EXTERNAL_LINK_TARGET_EVENT_NONE;
    state->target_received_bytes = 0U;
    state->target_requested_bytes = 0U;
    state->target_event_pending = 0U;
    state->target_rx_overflow = 0U;
    state->target_rx_size = 0U;
    state->target_response_size[0] = 0U;
    state->target_response_size[1] = 0U;
    state->target_read_index = 0U;
    state->target_read_size = 0U;
    state->target_preload_slot = 0U;
    state->target_active_slot = 0U;
    state->target_rx_slot = 1U;
    state->target_preload_active = 0U;
    state->target_read_active = 0U;
    memset(state->target_buffer, 0, sizeof(state->target_buffer));
}

static void reset_operation(k210_external_state_t *state)
{
    memset(&state->operation, 0, sizeof(state->operation));
}

static void stop_mode(k210_external_state_t *state)
{
    if(state->mode == HK_EXTERNAL_LINK_MODE_I2C_CONTROLLER ||
       state->mode == HK_EXTERNAL_LINK_MODE_I2C_TARGET)
        hal_external_i2c_stop();
    else if(state->mode == HK_EXTERNAL_LINK_MODE_UART && state->uart_baud != 0U)
        hal_external_uart_init(state->uart_baud);
    clear_target(state);
    reset_operation(state);
    state->mode = HK_EXTERNAL_LINK_MODE_UNCONFIGURED;
}

static hk_result_t validate_state(
    k210_external_state_t *state, const hk_lease_t *lease)
{
    if(!state || !state->active || !lease_equal(&state->lease, lease))
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t validate_mode_feature(
    k210_external_state_t *state, uint64_t feature)
{
    if((state->mode_features & feature) == 0U)
        return HK_ERR_NOT_DECLARED;
    return HK_OK;
}

static hk_result_t set_mode(k210_external_state_t *state, uint32_t mode)
{
    if(state->operation.state == K210_EXTERNAL_OP_IN_FLIGHT)
        return HK_ERR_BUSY;
    if(state->mode != HK_EXTERNAL_LINK_MODE_UNCONFIGURED)
        stop_mode(state);
    else
        clear_target(state);
    state->mode = mode;
    return HK_OK;
}

static hk_result_t k210_external_open(
    void *context, const hk_lease_t *lease, uint64_t mode_features)
{
    k210_external_state_t *state = (k210_external_state_t *)context;

    if(!state || !lease || mode_features == 0U ||
       (mode_features & ~HK_EXTERNAL_LINK_FEATURES_0_1) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(state->active)
        return HK_ERR_BUSY;
    memset(state, 0, sizeof(*state));
    state->lease = *lease;
    state->mode_features = mode_features;
    state->active = 1U;
    return HK_OK;
}

static hk_result_t k210_external_close(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(deadline_expired(deadline))
        return HK_ERR_DEADLINE_EXCEEDED;
    stop_mode(state);
    state->lease = HK_LEASE_NONE;
    state->mode_features = 0U;
    state->active = 0U;
    return HK_OK;
}

static hk_result_t k210_external_info(
    void *context, const hk_lease_t *lease, hk_external_link_info_t *info)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_external_link_info_t){
        sizeof(hk_external_link_info_t), HK_EXTERNAL_LINK_INFO_VERSION,
        HK_EXTERNAL_LINK_FEATURES_0_1,
        K210_EXTERNAL_UART_MIN_BAUD, K210_EXTERNAL_UART_MAX_BAUD,
        K210_EXTERNAL_I2C_MIN_HZ, K210_EXTERNAL_I2C_MAX_HZ,
        K210_EXTERNAL_POLL_BYTES,
        K210_EXTERNAL_MAX_BYTES, K210_EXTERNAL_MAX_BYTES,
        K210_EXTERNAL_MAX_BYTES, K210_EXTERNAL_MAX_BYTES, 0U,
    };
    return HK_OK;
}

static hk_result_t k210_external_mode(
    void *context, const hk_lease_t *lease, uint32_t *mode)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(!mode)
        return HK_ERR_INVALID_ARGUMENT;
    *mode = state->mode;
    return HK_OK;
}

static hk_result_t k210_external_configure_uart(
    void *context, const hk_lease_t *lease,
    const hk_external_link_uart_config_t *config)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    result = validate_mode_feature(state, HK_EXTERNAL_LINK_FEATURE_UART);
    if(result != HK_OK)
        return result;
    if(!config || config->baud < K210_EXTERNAL_UART_MIN_BAUD ||
       config->baud > K210_EXTERNAL_UART_MAX_BAUD)
        return HK_ERR_INVALID_ARGUMENT;
    result = set_mode(state, HK_EXTERNAL_LINK_MODE_UART);
    if(result != HK_OK)
        return result;
    state->uart_baud = config->baud;
    hal_external_uart_init(config->baud);
    return HK_OK;
}

static hk_result_t k210_external_configure_i2c_controller(
    void *context, const hk_lease_t *lease,
    const hk_external_link_i2c_controller_config_t *config)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    result = validate_mode_feature(
        state, HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER);
    if(result != HK_OK)
        return result;
    if(!config || config->frequency_hz < K210_EXTERNAL_I2C_MIN_HZ ||
       config->frequency_hz > K210_EXTERNAL_I2C_MAX_HZ)
        return HK_ERR_INVALID_ARGUMENT;
    result = set_mode(state, HK_EXTERNAL_LINK_MODE_I2C_CONTROLLER);
    if(result == HK_OK)
        state->i2c_frequency_hz = config->frequency_hz;
    return result;
}

static void target_receive(uint8_t byte)
{
    uint16_t size = s_external.target_rx_size;

    if(size < K210_EXTERNAL_MAX_BYTES)
        s_external.target_buffer[s_external.target_rx_slot][size++] = byte;
    else
        s_external.target_rx_overflow = 1U;
    s_external.target_rx_size = size;
}

static uint8_t target_transmit(void)
{
    uint32_t index;
    uint8_t slot;

    if(!s_external.target_read_active)
    {
        s_external.target_active_slot = s_external.target_preload_slot;
        s_external.target_read_size = s_external.target_preload_active ?
            s_external.target_response_size[s_external.target_preload_slot] : 0U;
        s_external.target_read_index = 0U;
        s_external.target_preload_active = 0U;
        s_external.target_read_active = 1U;
    }
    index = s_external.target_read_index++;
    slot = s_external.target_active_slot;
    if(index < s_external.target_read_size)
        return s_external.target_buffer[slot][index];
    return HK_EXTERNAL_LINK_TARGET_FILL_BYTE;
}

static void target_queue_event(uint32_t type, uint32_t received, uint32_t requested)
{
    if(s_external.target_event_pending)
        return;
    s_external.target_event_type = type;
    s_external.target_received_bytes = received;
    s_external.target_requested_bytes = requested;
    __sync_synchronize();
    s_external.target_event_pending = 1U;
}

static void target_event(hal_external_i2c_event_t event)
{
    if(event == HAL_EXTERNAL_I2C_EVENT_START)
        return;
    if(s_external.target_read_active)
    {
        target_queue_event(
            HK_EXTERNAL_LINK_TARGET_EVENT_READ, 0U,
            s_external.target_read_index);
        s_external.target_rx_slot = s_external.target_active_slot;
        s_external.target_read_active = 0U;
        s_external.target_read_index = 0U;
        s_external.target_read_size = 0U;
    }
    else if(s_external.target_rx_size != 0U ||
            s_external.target_rx_overflow)
    {
        target_queue_event(
            HK_EXTERNAL_LINK_TARGET_EVENT_WRITE,
            s_external.target_rx_size, 0U);
    }
}

static const hal_external_i2c_callbacks_t s_target_callbacks = {
    .receive = target_receive,
    .transmit = target_transmit,
    .event = target_event,
};

static hk_result_t k210_external_configure_i2c_target(
    void *context, const hk_lease_t *lease,
    const hk_external_link_i2c_target_config_t *config)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    result = validate_mode_feature(state, HK_EXTERNAL_LINK_FEATURE_I2C_TARGET);
    if(result != HK_OK)
        return result;
    if(!config || config->address > UINT16_C(0x7f))
        return HK_ERR_INVALID_ARGUMENT;
    result = set_mode(state, HK_EXTERNAL_LINK_MODE_I2C_TARGET);
    if(result != HK_OK)
        return result;
    hal_external_i2c_init((uint8_t)config->address, &s_target_callbacks);
    return HK_OK;
}

static hk_result_t check_begin(
    k210_external_state_t *state, hk_deadline_t deadline,
    const hk_cancel_t *cancel, hk_external_link_op_t *operation)
{
    if(!operation)
        return HK_ERR_INVALID_ARGUMENT;
    *operation = HK_EXTERNAL_LINK_OP_NONE;
    if(state->operation.state == K210_EXTERNAL_OP_IN_FLIGHT ||
       (state->operation.state == K210_EXTERNAL_OP_TERMINAL &&
        !state->operation.terminal_observed))
        return HK_ERR_BUSY;
    if(state->next_operation_generation == UINT32_MAX)
        return HK_ERR_LIMIT;
    if(cancellation_requested(cancel))
        return HK_ERR_CANCELLED;
    if(deadline_expired(deadline))
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static void start_operation(
    k210_external_state_t *state, uint32_t kind,
    const uint8_t *tx, uint32_t tx_size, uint8_t *rx, uint32_t rx_size,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    uint32_t generation = ++state->next_operation_generation;

    reset_operation(state);
    state->operation.state = K210_EXTERNAL_OP_IN_FLIGHT;
    state->operation.generation = generation;
    state->operation.kind = kind;
    state->operation.deadline = deadline;
    state->operation.tx = tx;
    state->operation.rx = rx;
    state->operation.tx_size = tx_size;
    state->operation.rx_size = rx_size;
    state->operation.terminal_result = HK_PENDING;
    if(cancel)
        state->operation.cancel = *cancel;
    operation->slot = K210_EXTERNAL_OPERATION_SLOT;
    operation->generation = generation;
}

static hk_result_t k210_external_uart_write_begin(
    void *context, const hk_lease_t *lease, const hk_buffer_view_t *tx,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(state->mode != HK_EXTERNAL_LINK_MODE_UART)
        return HK_ERR_INVALID_STATE;
    result = check_begin(state, deadline, cancel, operation);
    if(result != HK_OK)
        return result;
    start_operation(
        state, HK_EXTERNAL_LINK_OP_UART_WRITE,
        (const uint8_t *)tx->data, tx->size_bytes, NULL, 0U,
        deadline, cancel, operation);
    return HK_PENDING;
}

static hk_result_t k210_external_uart_read(
    void *context, const hk_lease_t *lease, hk_buffer_view_t *rx,
    uint32_t *received_bytes)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    size_t capacity;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(state->mode != HK_EXTERNAL_LINK_MODE_UART)
        return HK_ERR_INVALID_STATE;
    capacity = rx->size_bytes;
    if(capacity > K210_EXTERNAL_POLL_BYTES)
        capacity = K210_EXTERNAL_POLL_BYTES;
    *received_bytes = (uint32_t)hal_external_uart_receive(
        (uint8_t *)rx->data, capacity);
    return HK_OK;
}

static hk_result_t k210_external_i2c_transfer_begin(
    void *context, const hk_lease_t *lease,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(state->mode != HK_EXTERNAL_LINK_MODE_I2C_CONTROLLER)
        return HK_ERR_INVALID_STATE;
    result = check_begin(state, deadline, cancel, operation);
    if(result != HK_OK)
        return result;
    start_operation(
        state, HK_EXTERNAL_LINK_OP_I2C_TRANSFER,
        (const uint8_t *)transfer->tx.data, transfer->tx.size_bytes,
        (uint8_t *)transfer->rx.data, transfer->rx.size_bytes,
        deadline, cancel, operation);
    state->operation.i2c_address = transfer->address;
    return HK_PENDING;
}

static uint8_t operation_valid(
    const k210_external_state_t *state,
    const hk_external_link_op_t *operation)
{
    return (uint8_t)(operation &&
                     operation->slot == K210_EXTERNAL_OPERATION_SLOT &&
                     operation->generation != 0U &&
                     operation->generation == state->operation.generation &&
                     state->operation.state != K210_EXTERNAL_OP_NONE);
}

static void fill_progress(
    const k210_external_operation_t *operation,
    hk_external_link_op_progress_t *progress)
{
    uint32_t flags = 0U;

    if(operation->rx_done != 0U)
        flags |= HK_EXTERNAL_LINK_PROGRESS_RX_PREFIX_READABLE;
    if(operation->kind == HK_EXTERNAL_LINK_OP_UART_WRITE &&
       operation->tx_done == operation->tx_size &&
       operation->state == K210_EXTERNAL_OP_IN_FLIGHT)
        flags |= HK_EXTERNAL_LINK_PROGRESS_UART_DRAINING;
    if(operation->state == K210_EXTERNAL_OP_TERMINAL)
        flags |= HK_EXTERNAL_LINK_PROGRESS_TERMINAL;
    *progress = (hk_external_link_op_progress_t){
        sizeof(hk_external_link_op_progress_t),
        HK_EXTERNAL_LINK_OP_PROGRESS_VERSION,
        operation->kind, operation->tx_done, operation->rx_done,
        flags, operation->terminal_result, 0U,
    };
}

static hk_result_t latch_terminal(
    k210_external_state_t *state, hk_result_t result)
{
    k210_external_operation_t *operation = &state->operation;

    if(operation->state == K210_EXTERNAL_OP_TERMINAL)
        return operation->terminal_result;
    if(operation->kind == HK_EXTERNAL_LINK_OP_I2C_TRANSFER)
        hal_external_i2c_stop();
    else if(result != HK_OK && state->uart_baud != 0U)
        hal_external_uart_init(state->uart_baud);
    operation->state = K210_EXTERNAL_OP_TERMINAL;
    operation->terminal_result = result;
    return result;
}

static void poll_i2c(k210_external_state_t *state)
{
    k210_external_operation_t *operation = &state->operation;
    uint32_t budget = K210_EXTERNAL_POLL_BYTES;
    uint32_t steps = K210_EXTERNAL_POLL_BYTES;

    if(!operation->controller_started)
    {
        hal_external_i2c_controller_init(
            (uint8_t)operation->i2c_address, state->i2c_frequency_hz);
        operation->controller_started = 1U;
    }
    if(hal_external_i2c_controller_aborted())
    {
        (void)latch_terminal(state, HK_ERR_IO);
        return;
    }
    while(budget != 0U && operation->rx_done < operation->rx_size &&
          hal_external_i2c_controller_rx_ready())
    {
        operation->rx[operation->rx_done++] =
            hal_external_i2c_controller_read();
        --budget;
    }
    while(budget != 0U && operation->tx_done < operation->tx_size &&
          hal_external_i2c_controller_tx_ready())
    {
        hal_external_i2c_controller_write(
            operation->tx[operation->tx_done++]);
        --budget;
    }
    while(operation->tx_done == operation->tx_size &&
          operation->read_commands < operation->rx_size && steps != 0U &&
          hal_external_i2c_controller_tx_ready())
    {
        hal_external_i2c_controller_request_read();
        operation->read_commands++;
        --steps;
    }
    while(budget != 0U && operation->rx_done < operation->rx_size &&
          hal_external_i2c_controller_rx_ready())
    {
        operation->rx[operation->rx_done++] =
            hal_external_i2c_controller_read();
        --budget;
    }
    if(hal_external_i2c_controller_aborted())
        (void)latch_terminal(state, HK_ERR_IO);
    else if(operation->tx_done == operation->tx_size &&
            operation->read_commands == operation->rx_size &&
            operation->rx_done == operation->rx_size &&
            hal_external_i2c_controller_idle())
        (void)latch_terminal(state, HK_OK);
}

static hk_result_t k210_external_poll(
    void *context, const hk_lease_t *lease,
    const hk_external_link_op_t *operation_token,
    hk_external_link_op_progress_t *progress)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    k210_external_operation_t *operation;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(!operation_valid(state, operation_token))
        return HK_ERR_STALE_HANDLE;
    operation = &state->operation;
    if(operation->state == K210_EXTERNAL_OP_TERMINAL)
    {
        fill_progress(operation, progress);
        operation->terminal_observed = 1U;
        return operation->terminal_result;
    }
    if(cancellation_requested(&operation->cancel))
        result = latch_terminal(state, HK_ERR_CANCELLED);
    else if(deadline_expired(operation->deadline))
        result = latch_terminal(state, HK_ERR_DEADLINE_EXCEEDED);
    else if(operation->kind == HK_EXTERNAL_LINK_OP_UART_WRITE)
    {
        uint32_t remaining = operation->tx_size - operation->tx_done;

        if(remaining > K210_EXTERNAL_POLL_BYTES)
            remaining = K210_EXTERNAL_POLL_BYTES;
        operation->tx_done += (uint32_t)hal_external_uart_send_ready(
            operation->tx + operation->tx_done, remaining);
        if(operation->tx_done == operation->tx_size &&
           hal_external_uart_tx_idle())
            result = latch_terminal(state, HK_OK);
        else
            result = HK_PENDING;
    }
    else
    {
        poll_i2c(state);
        result = operation->state == K210_EXTERNAL_OP_TERMINAL ?
            operation->terminal_result : HK_PENDING;
    }
    if(operation->state == K210_EXTERNAL_OP_IN_FLIGHT &&
       operation->deadline.at_us == 0U)
        result = latch_terminal(state, HK_ERR_DEADLINE_EXCEEDED);
    fill_progress(operation, progress);
    if(operation->state == K210_EXTERNAL_OP_TERMINAL)
        operation->terminal_observed = 1U;
    return result;
}

static hk_result_t k210_external_cancel(
    void *context, const hk_lease_t *lease,
    const hk_external_link_op_t *operation_token,
    hk_external_link_op_progress_t *progress)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(!operation_valid(state, operation_token))
        return HK_ERR_STALE_HANDLE;
    if(state->operation.state == K210_EXTERNAL_OP_IN_FLIGHT)
        result = latch_terminal(state, HK_ERR_CANCELLED);
    else
        result = state->operation.terminal_result;
    fill_progress(&state->operation, progress);
    state->operation.terminal_observed = 1U;
    return result;
}

static hk_result_t k210_external_target_poll(
    void *context, const hk_lease_t *lease, hk_buffer_view_t *rx,
    hk_external_link_target_event_t *event)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    uint32_t type;
    uint32_t received;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(state->mode != HK_EXTERNAL_LINK_MODE_I2C_TARGET)
        return HK_ERR_INVALID_STATE;
    *event = (hk_external_link_target_event_t){
        sizeof(hk_external_link_target_event_t),
        HK_EXTERNAL_LINK_TARGET_EVENT_VERSION,
        HK_EXTERNAL_LINK_TARGET_EVENT_NONE, 0U, 0U, 0U,
    };
    if(!state->target_event_pending)
        return HK_PENDING;
    __sync_synchronize();
    type = state->target_event_type;
    received = state->target_received_bytes;
    if(state->target_rx_overflow)
        return HK_ERR_LIMIT;
    if(type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
       rx->size_bytes < received)
        return HK_ERR_LIMIT;
    if(type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE && received != 0U)
        memcpy(rx->data, state->target_buffer[state->target_rx_slot], received);
    event->type = type;
    event->received_bytes = received;
    event->requested_bytes = state->target_requested_bytes;
    state->target_event_pending = 0U;
    state->target_event_type = HK_EXTERNAL_LINK_TARGET_EVENT_NONE;
    state->target_received_bytes = 0U;
    state->target_requested_bytes = 0U;
    state->target_rx_overflow = 0U;
    state->target_rx_size = 0U;
    memset(state->target_buffer[state->target_rx_slot], 0,
           sizeof(state->target_buffer[state->target_rx_slot]));
    return HK_OK;
}

static hk_result_t k210_external_target_preload(
    void *context, const hk_lease_t *lease, const hk_buffer_view_t *tx)
{
    k210_external_state_t *state = (k210_external_state_t *)context;
    uint8_t slot;
    uint32_t irq_state;
    hk_result_t result = validate_state(state, lease);

    if(result != HK_OK)
        return result;
    if(state->mode != HK_EXTERNAL_LINK_MODE_I2C_TARGET)
        return HK_ERR_INVALID_STATE;
    irq_state = hal_external_i2c_target_lock();
    if(state->target_read_active)
        slot = (uint8_t)(1U - state->target_active_slot);
    else if(state->target_preload_active)
        slot = state->target_preload_slot;
    else
        slot = (uint8_t)(1U - state->target_rx_slot);
    memset(state->target_buffer[slot], 0,
           sizeof(state->target_buffer[slot]));
    if(tx->size_bytes != 0U)
        memcpy(state->target_buffer[slot], tx->data, tx->size_bytes);
    state->target_response_size[slot] = (uint16_t)tx->size_bytes;
    state->target_preload_slot = slot;
    __sync_synchronize();
    state->target_preload_active = (uint8_t)(tx->size_bytes != 0U);
    hal_external_i2c_target_unlock(irq_state);
    return HK_OK;
}

static hk_result_t k210_external_cleanup_lease(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    hk_external_link_provider_t *provider =
        (hk_external_link_provider_t *)context;
    k210_external_state_t *state;

    if(!provider || !provider->context)
        return HK_ERR_INTERNAL;
    state = (k210_external_state_t *)provider->context;
    if(!state->active || !lease_equal(&state->lease, lease))
        return HK_OK;
    return k210_external_close(state, lease, deadline);
}

static hk_result_t k210_external_cleanup(
    void *context, hk_owner_t owner, hk_deadline_t deadline)
{
    hk_external_link_provider_t *provider =
        (hk_external_link_provider_t *)context;
    k210_external_state_t *state;

    if(!provider || !provider->context)
        return HK_ERR_INTERNAL;
    state = (k210_external_state_t *)provider->context;
    if(!state->active || !owner_equal(state->lease.owner, owner))
        return HK_OK;
    return k210_external_close(state, &state->lease, deadline);
}

static hk_result_t k210_external_cleanup_dispatch(
    void *context, hk_owner_t owner, uint16_t target_core,
    hk_deadline_t deadline)
{
    if(target_core != 0U)
        return HK_ERR_WRONG_CONTEXT;
    return k210_external_cleanup(context, owner, deadline);
}

static hk_external_link_provider_t s_external_provider = {
    .context = &s_external,
    .open = k210_external_open,
    .close = k210_external_close,
    .get_info = k210_external_info,
    .get_mode = k210_external_mode,
    .configure_uart = k210_external_configure_uart,
    .configure_i2c_controller = k210_external_configure_i2c_controller,
    .configure_i2c_target = k210_external_configure_i2c_target,
    .uart_write_begin = k210_external_uart_write_begin,
    .uart_read = k210_external_uart_read,
    .i2c_transfer_begin = k210_external_i2c_transfer_begin,
    .poll = k210_external_poll,
    .cancel = k210_external_cancel,
    .target_poll = k210_external_target_poll,
    .target_preload = k210_external_target_preload,
};

const hk_capability_provider_t hk_k210_external_link_provider = {
    .context = &s_external_provider,
    .cleanup_lease = k210_external_cleanup_lease,
    .cleanup = k210_external_cleanup,
    .cleanup_dispatch = k210_external_cleanup_dispatch,
    .max_leases = 1U,
};
