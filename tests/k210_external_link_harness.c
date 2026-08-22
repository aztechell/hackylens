#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/external_link.h>

#include "capability_core_binding.h"
#include "capability_provider.h"
#include "external_link_normative_suite.h"
#include "hal_external_link.h"

#define TEST_LEASES 2U
#define TEST_BYTES 256U

#define HARNESS_CHECK(condition)                                             \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            fprintf(stderr, "K210 target CHECK failed at line %d: %s\n",   \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while(0)

extern const hk_capability_provider_t hk_k210_external_link_provider;

static hk_lease_t s_leases[TEST_LEASES];
static uint32_t s_lease_generations[TEST_LEASES];
static uint64_t s_now_us;
static uint8_t s_uart_tx[TEST_BYTES * 2U];
static uint32_t s_uart_tx_size;
static uint8_t s_i2c_source[TEST_BYTES];
static uint32_t s_i2c_source_size;
static uint32_t s_i2c_source_position;
static uint8_t s_i2c_rx[TEST_BYTES];
static uint32_t s_i2c_rx_head;
static uint32_t s_i2c_rx_size;
static uint8_t s_i2c_tx[TEST_BYTES];
static uint32_t s_i2c_tx_size;
static uint8_t s_i2c_aborted;
static uint8_t s_i2c_active;
static uint8_t s_provider_quarantined;
static const hal_external_i2c_callbacks_t *s_target_callbacks;
static uint8_t s_target_locked;
static uint8_t s_inject_on_unlock;
static uint8_t s_unlock_write[TEST_BYTES];
static uint32_t s_unlock_write_size;
static uint32_t s_target_lock_calls;
static uint32_t s_target_unlock_calls;

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

hk_result_t capability_owner_runtime_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    hk_capability_id_t expected_type, hk_lease_t *lease)
{
    if(!request || !lease || hk_owner_is_zero(owner) ||
       expected_type != HK_CAPABILITY_ID_EXTERNAL_LINK ||
       request->id != HK_CAPABILITY_ID_EXTERNAL_LINK ||
       (request->required_features & ~HK_EXTERNAL_LINK_FEATURES_0_1) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(s_provider_quarantined)
        return HK_ERR_INVALID_STATE;
    for(uint32_t slot = 0U; slot < TEST_LEASES; ++slot)
    {
        if(!hk_lease_is_zero(&s_leases[slot]))
            continue;
        if(++s_lease_generations[slot] == 0U)
            return HK_ERR_LIMIT;
        s_leases[slot] = (hk_lease_t){
            slot, s_lease_generations[slot], owner,
            HK_CAPABILITY_ID_EXTERNAL_LINK,
        };
        *lease = s_leases[slot];
        return HK_OK;
    }
    return HK_ERR_LIMIT;
}

static hk_result_t validate_runtime_lease(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type, uint8_t allow_quarantined,
    void **provider_context)
{
    if(!lease || !provider_context ||
       expected_type != HK_CAPABILITY_ID_EXTERNAL_LINK ||
       lease->capability_id != HK_CAPABILITY_ID_EXTERNAL_LINK ||
       lease->slot >= TEST_LEASES)
        return HK_ERR_INVALID_ARGUMENT;
    if(!owner_equal(owner, lease->owner))
        return HK_ERR_WRONG_OWNER;
    if(!lease_equal(lease, &s_leases[lease->slot]))
        return HK_ERR_STALE_HANDLE;
    if(s_provider_quarantined && !allow_quarantined)
        return HK_ERR_INVALID_STATE;
    *provider_context = hk_k210_external_link_provider.context;
    return HK_OK;
}

hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type, void **provider_context)
{
    return validate_runtime_lease(
        owner, lease, expected_type, 0U, provider_context);
}

hk_result_t capability_owner_runtime_release(
    hk_owner_t owner, hk_capability_id_t expected_type,
    hk_deadline_t deadline, hk_lease_t *lease)
{
    void *context;
    hk_result_t result;

    (void)deadline;
    if(!lease)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_runtime_lease(
        owner, lease, expected_type, 1U, &context);
    if(result != HK_OK)
        return result;
    s_leases[lease->slot] = HK_LEASE_NONE;
    *lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t capability_owner_runtime_quarantine(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type)
{
    void *context;
    hk_result_t result = validate_runtime_lease(
        owner, lease, expected_type, 1U, &context);

    if(result == HK_OK)
        s_provider_quarantined = 1U;
    return result;
}

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void hal_external_uart_init(uint32_t baud)
{
    (void)baud;
    s_i2c_active = 0U;
    s_target_callbacks = NULL;
}

size_t hal_external_uart_receive(uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return 0U;
}

void hal_external_uart_send(const uint8_t *data, size_t len)
{
    (void)hal_external_uart_send_ready(data, len);
}

size_t hal_external_uart_send_ready(const uint8_t *data, size_t len)
{
    size_t capacity = sizeof(s_uart_tx) - s_uart_tx_size;

    if(!data)
        return 0U;
    if(len > capacity)
        len = capacity;
    memcpy(s_uart_tx + s_uart_tx_size, data, len);
    s_uart_tx_size += (uint32_t)len;
    return len;
}

uint8_t hal_external_uart_tx_idle(void)
{
    return 1U;
}

void hal_external_i2c_init(
    uint8_t address, const hal_external_i2c_callbacks_t *callbacks)
{
    (void)address;
    s_i2c_active = 1U;
    s_target_callbacks = callbacks;
}

void hal_external_i2c_controller_init(
    uint8_t address, uint32_t frequency_hz)
{
    (void)address;
    (void)frequency_hz;
    s_i2c_active = 1U;
    s_target_callbacks = NULL;
    s_i2c_rx_head = 0U;
    s_i2c_rx_size = 0U;
}

uint8_t hal_external_i2c_controller_aborted(void)
{
    return s_i2c_aborted;
}

uint8_t hal_external_i2c_controller_tx_ready(void)
{
    return 1U;
}

uint8_t hal_external_i2c_controller_rx_ready(void)
{
    return (uint8_t)(s_i2c_rx_head < s_i2c_rx_size);
}

uint8_t hal_external_i2c_controller_idle(void)
{
    return (uint8_t)(s_i2c_rx_head == s_i2c_rx_size);
}

void hal_external_i2c_controller_write(uint8_t byte)
{
    if(s_i2c_tx_size < sizeof(s_i2c_tx))
        s_i2c_tx[s_i2c_tx_size++] = byte;
}

void hal_external_i2c_controller_request_read(void)
{
    uint8_t value = s_i2c_source_position < s_i2c_source_size ?
        s_i2c_source[s_i2c_source_position] :
        (uint8_t)(0x80U + s_i2c_source_position);

    s_i2c_source_position++;
    if(s_i2c_rx_size < sizeof(s_i2c_rx))
        s_i2c_rx[s_i2c_rx_size++] = value;
}

uint8_t hal_external_i2c_controller_read(void)
{
    return s_i2c_rx[s_i2c_rx_head++];
}

uint32_t hal_external_i2c_target_lock(void)
{
    s_target_locked = 1U;
    s_target_lock_calls++;
    return s_i2c_active;
}

void hal_external_i2c_target_unlock(uint32_t state)
{
    (void)state;
    s_target_locked = 0U;
    s_target_unlock_calls++;
    if(s_inject_on_unlock && s_target_callbacks)
    {
        uint32_t size = s_unlock_write_size;

        s_inject_on_unlock = 0U;
        s_unlock_write_size = 0U;
        for(uint32_t index = 0U; index < size; ++index)
            s_target_callbacks->receive(s_unlock_write[index]);
        s_target_callbacks->event(HAL_EXTERNAL_I2C_EVENT_STOP);
    }
}

void hal_external_i2c_stop(void)
{
    s_i2c_active = 0U;
    s_target_callbacks = NULL;
    s_i2c_rx_head = 0U;
    s_i2c_rx_size = 0U;
}

static void backend_reset(void)
{
    memset(s_leases, 0, sizeof(s_leases));
    memset(s_lease_generations, 0, sizeof(s_lease_generations));
    memset(s_uart_tx, 0, sizeof(s_uart_tx));
    memset(s_i2c_source, 0, sizeof(s_i2c_source));
    memset(s_i2c_rx, 0, sizeof(s_i2c_rx));
    memset(s_i2c_tx, 0, sizeof(s_i2c_tx));
    s_now_us = 0U;
    s_uart_tx_size = 0U;
    s_i2c_source_size = 0U;
    s_i2c_source_position = 0U;
    s_i2c_rx_head = 0U;
    s_i2c_rx_size = 0U;
    s_i2c_tx_size = 0U;
    s_i2c_aborted = 0U;
    s_i2c_active = 0U;
    s_provider_quarantined = 0U;
    s_target_callbacks = NULL;
    s_target_locked = 0U;
    s_inject_on_unlock = 0U;
    memset(s_unlock_write, 0, sizeof(s_unlock_write));
    s_unlock_write_size = 0U;
    s_target_lock_calls = 0U;
    s_target_unlock_calls = 0U;
}

static void backend_set_now(uint64_t now_us)
{
    s_now_us = now_us;
}

static void backend_set_i2c_rx(
    const uint8_t *bytes, uint32_t size_bytes)
{
    if(size_bytes > sizeof(s_i2c_source))
        size_bytes = sizeof(s_i2c_source);
    memcpy(s_i2c_source, bytes, size_bytes);
    s_i2c_source_size = size_bytes;
    s_i2c_source_position = 0U;
}

static void backend_target_write(
    const uint8_t *bytes, uint32_t size_bytes)
{
    if(!s_target_callbacks)
        return;
    for(uint32_t i = 0U; i < size_bytes; ++i)
        s_target_callbacks->receive(bytes[i]);
    s_target_callbacks->event(HAL_EXTERNAL_I2C_EVENT_STOP);
}

static void backend_target_read(uint8_t *bytes, uint32_t size_bytes)
{
    if(!s_target_callbacks)
        return;
    for(uint32_t i = 0U; i < size_bytes; ++i)
        bytes[i] = s_target_callbacks->transmit();
    s_target_callbacks->event(HAL_EXTERNAL_I2C_EVENT_STOP);
}

static uint32_t backend_uart_tx_bytes(void)
{
    return s_uart_tx_size;
}

static int run_target_handoff_tests(void)
{
    static const hk_owner_t owner = {21U, 23U};
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t link = {0};
    hk_external_link_i2c_target_config_t target = {
        sizeof(target), HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION,
        0x32U, 0U, 0U,
    };
    hk_external_link_target_event_t event;
    uint8_t receive[TEST_BYTES] = {0};
    hk_buffer_view_t rx = {
        receive, sizeof(receive), 0U, HK_BUFFER_ACCESS_WRITABLE,
    };
    uint8_t write1[] = {1U, 2U};
    uint8_t write2[] = {3U, 4U, 5U};
    uint8_t write3[] = {6U, 7U, 8U, 9U};
    uint8_t response[] = {0xa1U, 0xa2U};
    uint8_t next_response[] = {0xb1U, 0xb2U};
    uint8_t observed[3] = {0};
    uint8_t next_observed[3] = {0};
    hk_buffer_view_t tx = {
        response, sizeof(response), 0U, HK_BUFFER_ACCESS_READABLE,
    };
    hk_buffer_view_t next_tx = {
        next_response, sizeof(next_response), 0U, HK_BUFFER_ACCESS_READABLE,
    };

    backend_reset();
    backend_set_now(100U);
    request.required_features = HK_EXTERNAL_LINK_FEATURE_I2C_TARGET;
    HARNESS_CHECK(hk_external_link_acquire(
        owner, &request, HK_EXTERNAL_LINK_FEATURE_I2C_TARGET, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_i2c_target(
        owner, &link, &target) == HK_OK);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write1));
    HARNESS_CHECK(memcmp(receive, write1, sizeof(write1)) == 0);
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write2));
    HARNESS_CHECK(memcmp(receive, write2, sizeof(write2)) == 0);

    HARNESS_CHECK(hk_external_link_i2c_target_preload_response(
        owner, &link, &tx) == HK_OK);
    backend_target_read(observed, sizeof(observed));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(observed[0] == response[0] &&
                  observed[1] == response[1] && observed[2] == 0U);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_READ &&
                  event.requested_bytes == sizeof(observed));
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write3));
    HARNESS_CHECK(memcmp(receive, write3, sizeof(write3)) == 0);

    backend_target_write(write1, sizeof(write1));
    HARNESS_CHECK(hk_external_link_i2c_target_preload_response(
        owner, &link, &tx) == HK_OK);
    observed[0] = s_target_callbacks->transmit();
    HARNESS_CHECK(hk_external_link_i2c_target_preload_response(
        owner, &link, &next_tx) == HK_OK);
    observed[1] = s_target_callbacks->transmit();
    observed[2] = s_target_callbacks->transmit();
    s_target_callbacks->event(HAL_EXTERNAL_I2C_EVENT_STOP);
    HARNESS_CHECK(observed[0] == response[0] &&
                  observed[1] == response[1] && observed[2] == 0U);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_ERR_OVERFLOW);
    backend_target_read(next_observed, sizeof(next_observed));
    HARNESS_CHECK(next_observed[0] == next_response[0] &&
                  next_observed[1] == next_response[1] &&
                  next_observed[2] == 0U);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_READ &&
                  event.requested_bytes == sizeof(next_observed));

    backend_target_write(write1, sizeof(write1));
    memcpy(s_unlock_write, write2, sizeof(write2));
    s_unlock_write_size = sizeof(write2);
    s_inject_on_unlock = 1U;
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write1));
    HARNESS_CHECK(memcmp(receive, write1, sizeof(write1)) == 0);
    HARNESS_CHECK(!s_target_locked && !s_inject_on_unlock);
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write2));
    HARNESS_CHECK(memcmp(receive, write2, sizeof(write2)) == 0);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_ERR_OVERFLOW);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_NONE);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_PENDING);
    backend_target_write(write3, sizeof(write3));
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write3));
    HARNESS_CHECK(memcmp(receive, write3, sizeof(write3)) == 0);
    HARNESS_CHECK(s_target_lock_calls == s_target_unlock_calls);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(hk_external_link_configure_i2c_target(
        owner, &link, &target) == HK_OK);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_PENDING);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(hk_external_link_release(
        owner, (hk_deadline_t){1000U}, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_acquire(
        owner, &request, HK_EXTERNAL_LINK_FEATURE_I2C_TARGET, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_i2c_target(
        owner, &link, &target) == HK_OK);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(
        owner, &link, &rx, &event) == HK_PENDING);
    HARNESS_CHECK(hk_external_link_release(
        owner, (hk_deadline_t){1000U}, &link) == HK_OK);
    return 0;
}

int main(void)
{
    const external_link_normative_backend_t backend = {
        backend_reset,
        backend_set_now,
        backend_set_i2c_rx,
        backend_target_write,
        backend_target_read,
        backend_uart_tx_bytes,
    };

    if(run_target_handoff_tests() != 0)
        return 1;
    if(external_link_normative_suite_run(&backend) != 0)
        return 1;
    printf("K210_EXTERNAL_LINK_OK normative=1 target_handoff=1 uart=%u i2c_tx=%u\n",
           (unsigned)s_uart_tx_size, (unsigned)s_i2c_tx_size);
    return 0;
}
