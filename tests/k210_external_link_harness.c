#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/external_link.h>

#include "external_link_provider.h"
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


static uint64_t s_now_us;
static uint8_t s_uart_tx[TEST_BYTES * 2U];
static uint32_t s_uart_tx_size;
static uint8_t s_uart_rx[TEST_BYTES * 2U];
static uint32_t s_uart_rx_head;
static uint32_t s_uart_rx_size;
static uint8_t s_uart_loopback;
static hal_external_uart_receive_fn s_uart_receive;
static void *s_uart_receive_context;
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
static uint8_t s_target_locked;
static uint8_t s_inject_on_unlock;
static uint8_t s_unlock_write[TEST_BYTES];
static uint32_t s_unlock_write_size;
static uint32_t s_target_lock_calls;
static uint32_t s_target_unlock_calls;

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void hal_external_uart_init(uint32_t baud,
                            hal_external_uart_receive_fn receive,
                            void *context)
{
    (void)baud;
    s_i2c_active = 0U;
    s_target_callbacks = NULL;
    s_uart_rx_head = 0U;
    s_uart_rx_size = 0U;
    s_uart_receive = receive;
    s_uart_receive_context = context;
}

void hal_external_uart_stop(void)
{
    s_uart_receive = NULL;
    s_uart_receive_context = NULL;
    s_uart_rx_head = 0U;
    s_uart_rx_size = 0U;
}

size_t hal_external_uart_receive(uint8_t *data, size_t len)
{
    size_t available = s_uart_rx_size - s_uart_rx_head;

    if(len > available)
        len = available;
    if(len != 0U)
        memcpy(data, s_uart_rx + s_uart_rx_head, len);
    s_uart_rx_head += (uint32_t)len;
    if(s_uart_rx_head == s_uart_rx_size)
    {
        s_uart_rx_head = 0U;
        s_uart_rx_size = 0U;
    }
    return len;
}

void hal_external_uart_send(const uint8_t *data, size_t len)
{
    (void)hal_external_uart_send_ready(data, len);
}

size_t hal_external_uart_send_ready(const uint8_t *data, size_t len)
{
    size_t capacity = sizeof(s_uart_tx) - s_uart_tx_size;
    size_t sent;

    if(!data)
        return 0U;
    if(len > capacity)
        len = capacity;
    sent = len;
    memcpy(s_uart_tx + s_uart_tx_size, data, len);
    s_uart_tx_size += (uint32_t)len;
    if(s_uart_loopback)
    {
        for(size_t index = 0U; index < len; ++index)
        {
            if(s_uart_receive)
                s_uart_receive(s_uart_receive_context, data[index]);
        }
    }
    return sent;
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

void hal_external_i2c_controller_start(
    uint8_t address, uint32_t frequency_hz,
    const uint8_t *tx, uint32_t tx_size,
    uint8_t *rx, uint32_t rx_size)
{
    (void)address;
    (void)frequency_hz;
    s_i2c_active = 1U;
    s_target_callbacks = NULL;
    s_i2c_rx_head = 0U;
    s_i2c_rx_size = rx_size;
    s_i2c_tx_size = tx_size;
    if(tx_size != 0U)
        memcpy(s_i2c_tx, tx, tx_size);
    for(uint32_t index = 0U; index < rx_size; ++index)
    {
        uint8_t value = s_i2c_source_position < s_i2c_source_size ?
            s_i2c_source[s_i2c_source_position] :
            (uint8_t)(0x80U + s_i2c_source_position);

        s_i2c_source_position++;
        rx[index] = value;
    }
}

uint8_t hal_external_i2c_controller_aborted(void)
{
    return s_i2c_aborted;
}

uint32_t hal_external_i2c_controller_tx_accepted(void)
{
    return s_i2c_tx_size;
}

uint32_t hal_external_i2c_controller_rx_received(void)
{
    return s_i2c_rx_size;
}

uint8_t hal_external_i2c_controller_idle(void)
{
    return (uint8_t)(!s_i2c_aborted && s_i2c_active);
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
    *hk_external_link_binding.state = (hk_external_link_state_t){0};
    memset(s_uart_tx, 0, sizeof(s_uart_tx));
    memset(s_uart_rx, 0, sizeof(s_uart_rx));
    memset(s_i2c_source, 0, sizeof(s_i2c_source));
    memset(s_i2c_rx, 0, sizeof(s_i2c_rx));
    memset(s_i2c_tx, 0, sizeof(s_i2c_tx));
    s_now_us = 0U;
    s_uart_tx_size = 0U;
    s_uart_rx_head = 0U;
    s_uart_rx_size = 0U;
    s_uart_loopback = 0U;
    s_uart_receive = NULL;
    s_uart_receive_context = NULL;
    s_i2c_source_size = 0U;
    s_i2c_source_position = 0U;
    s_i2c_rx_head = 0U;
    s_i2c_rx_size = 0U;
    s_i2c_tx_size = 0U;
    s_i2c_aborted = 0U;
    s_i2c_active = 0U;
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

static int backend_uart_receive(const uint8_t *data, size_t size)
{
    HARNESS_CHECK(s_uart_receive != NULL);
    for(size_t index = 0U; index < size; ++index)
        s_uart_receive(s_uart_receive_context, data[index]);
    return 0;
}

static int run_uart_loopback_capture_tests(void)
{
    hk_external_link_t link = {0};
    hk_external_link_uart_config_t uart = {
        sizeof(uart), HK_EXTERNAL_LINK_UART_CONFIG_VERSION, 115200U, 0U,
    };
    hk_external_link_op_t operation = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_progress_t progress;
    uint8_t transmit[TEST_BYTES];
    uint8_t receive[TEST_BYTES] = {0};
    uint8_t chunk[32];
    hk_buffer_view_t tx = {
        transmit, sizeof(transmit), 0U, HK_BUFFER_ACCESS_READABLE,
    };
    hk_buffer_view_t rx = {
        chunk, sizeof(chunk), 0U, HK_BUFFER_ACCESS_WRITABLE,
    };
    uint32_t total = 0U;
    uint32_t received = 0U;
    hk_result_t result = HK_PENDING;

    for(uint32_t index = 0U; index < sizeof(transmit); ++index)
        transmit[index] = (uint8_t)(index * 73U + 19U);

    backend_reset();
    HARNESS_CHECK(hk_external_link_open(hk_external_link_service(), HK_EXTERNAL_LINK_FEATURE_UART, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_uart(&link, &uart) == HK_OK);
    s_uart_loopback = 1U;
    HARNESS_CHECK(hk_external_link_uart_write_begin(&link, &tx, (hk_deadline_t){1000U}, NULL, &operation) == HK_PENDING);
    for(uint32_t poll = 0U; poll < 16U && result == HK_PENDING; ++poll)
        result = hk_external_link_poll(&link, &operation, &progress);
    HARNESS_CHECK(result == HK_OK);
    while(total < sizeof(receive))
    {
        memset(chunk, 0, sizeof(chunk));
        HARNESS_CHECK(hk_external_link_uart_read(&link, &rx, &received) == HK_OK);
        HARNESS_CHECK(received != 0U && received <= sizeof(chunk));
        memcpy(receive + total, chunk, received);
        total += received;
    }
    HARNESS_CHECK(total == sizeof(transmit));
    HARNESS_CHECK(memcmp(receive, transmit, sizeof(transmit)) == 0);

    HARNESS_CHECK(hk_external_link_configure_uart(&link, &uart) == HK_OK);
    HARNESS_CHECK(hk_external_link_uart_read(&link, &rx, &received) == HK_OK && received == 0U);

    operation = HK_EXTERNAL_LINK_OP_NONE;
    HARNESS_CHECK(hk_external_link_uart_write_begin(&link, &tx, (hk_deadline_t){1000U}, NULL, &operation) == HK_PENDING);
    HARNESS_CHECK(hk_external_link_poll(&link, &operation, &progress) == HK_PENDING);
    HARNESS_CHECK(hk_external_link_cancel(&link, &operation, &progress) == HK_ERR_CANCELLED);
    HARNESS_CHECK(hk_external_link_close(&link, (hk_deadline_t){1000U}) == HK_OK);
    HARNESS_CHECK(hk_external_link_open(hk_external_link_service(), HK_EXTERNAL_LINK_FEATURE_UART, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_uart(&link, &uart) == HK_OK);
    HARNESS_CHECK(hk_external_link_uart_read(&link, &rx, &received) == HK_OK && received == 0U);
    HARNESS_CHECK(hk_external_link_close(&link, (hk_deadline_t){1000U}) == HK_OK);

    backend_reset();
    HARNESS_CHECK(hk_external_link_open(hk_external_link_service(), HK_EXTERNAL_LINK_FEATURE_UART, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_uart(&link, &uart) == HK_OK);
    s_uart_loopback = 1U;
    memset(chunk, 0x5a, sizeof(chunk));
    HARNESS_CHECK(backend_uart_receive(chunk, sizeof(chunk)) == 0);
    operation = HK_EXTERNAL_LINK_OP_NONE;
    result = HK_PENDING;
    HARNESS_CHECK(hk_external_link_uart_write_begin(&link, &tx, (hk_deadline_t){1000U}, NULL, &operation) == HK_PENDING);
    for(uint32_t poll = 0U; poll < 16U && result == HK_PENDING; ++poll)
        result = hk_external_link_poll(&link, &operation, &progress);
    HARNESS_CHECK(result == HK_OK);
    HARNESS_CHECK(hk_external_link_uart_read(&link, &rx, &received) == HK_ERR_OVERFLOW);
    HARNESS_CHECK(received == 0U);
    HARNESS_CHECK(hk_external_link_uart_read(&link, &rx, &received) == HK_OK && received == 0U);
    HARNESS_CHECK(hk_external_link_close(&link, (hk_deadline_t){1000U}) == HK_OK);
    return 0;
}

static int run_target_handoff_tests(void)
{
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
    HARNESS_CHECK(hk_external_link_open(hk_external_link_service(), HK_EXTERNAL_LINK_FEATURE_I2C_TARGET, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_i2c_target(&link, &target) == HK_OK);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write1));
    HARNESS_CHECK(memcmp(receive, write1, sizeof(write1)) == 0);
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write2));
    HARNESS_CHECK(memcmp(receive, write2, sizeof(write2)) == 0);

    HARNESS_CHECK(hk_external_link_i2c_target_preload_response(&link, &tx) == HK_OK);
    backend_target_read(observed, sizeof(observed));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(observed[0] == response[0] &&
                  observed[1] == response[1] && observed[2] == 0U);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_READ &&
                  event.requested_bytes == sizeof(observed));
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write3));
    HARNESS_CHECK(memcmp(receive, write3, sizeof(write3)) == 0);

    backend_target_write(write1, sizeof(write1));
    HARNESS_CHECK(hk_external_link_i2c_target_preload_response(&link, &tx) == HK_OK);
    observed[0] = s_target_callbacks->transmit();
    HARNESS_CHECK(hk_external_link_i2c_target_preload_response(&link, &next_tx) == HK_OK);
    observed[1] = s_target_callbacks->transmit();
    observed[2] = s_target_callbacks->transmit();
    s_target_callbacks->event(HAL_EXTERNAL_I2C_EVENT_STOP);
    HARNESS_CHECK(observed[0] == response[0] &&
                  observed[1] == response[1] && observed[2] == 0U);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_ERR_OVERFLOW);
    backend_target_read(next_observed, sizeof(next_observed));
    HARNESS_CHECK(next_observed[0] == next_response[0] &&
                  next_observed[1] == next_response[1] &&
                  next_observed[2] == 0U);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_READ &&
                  event.requested_bytes == sizeof(next_observed));

    backend_target_write(write1, sizeof(write1));
    memcpy(s_unlock_write, write2, sizeof(write2));
    s_unlock_write_size = sizeof(write2);
    s_inject_on_unlock = 1U;
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write1));
    HARNESS_CHECK(memcmp(receive, write1, sizeof(write1)) == 0);
    HARNESS_CHECK(!s_target_locked && !s_inject_on_unlock);
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write2));
    HARNESS_CHECK(memcmp(receive, write2, sizeof(write2)) == 0);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_ERR_OVERFLOW);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_NONE);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_PENDING);
    backend_target_write(write3, sizeof(write3));
    memset(receive, 0, sizeof(receive));
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_OK);
    HARNESS_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
                  event.received_bytes == sizeof(write3));
    HARNESS_CHECK(memcmp(receive, write3, sizeof(write3)) == 0);
    HARNESS_CHECK(s_target_lock_calls == s_target_unlock_calls);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(hk_external_link_configure_i2c_target(&link, &target) == HK_OK);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_PENDING);

    backend_target_write(write1, sizeof(write1));
    backend_target_write(write2, sizeof(write2));
    backend_target_write(write3, sizeof(write3));
    HARNESS_CHECK(hk_external_link_close(&link, (hk_deadline_t){1000U}) == HK_OK);
    HARNESS_CHECK(hk_external_link_open(hk_external_link_service(), HK_EXTERNAL_LINK_FEATURE_I2C_TARGET, &link) == HK_OK);
    HARNESS_CHECK(hk_external_link_configure_i2c_target(&link, &target) == HK_OK);
    HARNESS_CHECK(hk_external_link_i2c_target_poll(&link, &rx, &event) == HK_PENDING);
    HARNESS_CHECK(hk_external_link_close(&link, (hk_deadline_t){1000U}) == HK_OK);
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

    if(run_uart_loopback_capture_tests() != 0)
        return 1;
    if(run_target_handoff_tests() != 0)
        return 1;
    if(external_link_normative_suite_run(&backend) != 0)
        return 1;
    printf("K210_EXTERNAL_LINK_OK normative=1 target_handoff=1 uart_loopback=1 uart=%u i2c_tx=%u\n",
           (unsigned)s_uart_tx_size, (unsigned)s_i2c_tx_size);
    return 0;
}
