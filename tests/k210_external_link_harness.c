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
static const hal_external_i2c_callbacks_t *s_target_callbacks;

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

hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type, void **provider_context)
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
    *provider_context = hk_k210_external_link_provider.context;
    return HK_OK;
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
    result = capability_owner_runtime_validate(
        owner, lease, expected_type, &context);
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
    return capability_owner_runtime_validate(
        owner, lease, expected_type, &context);
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
    return s_i2c_active;
}

void hal_external_i2c_target_unlock(uint32_t state)
{
    (void)state;
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
    s_target_callbacks = NULL;
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

    if(external_link_normative_suite_run(&backend) != 0)
        return 1;
    printf("K210_EXTERNAL_LINK_OK normative=1 uart=%u i2c_tx=%u\n",
           (unsigned)s_uart_tx_size, (unsigned)s_i2c_tx_size);
    return 0;
}
