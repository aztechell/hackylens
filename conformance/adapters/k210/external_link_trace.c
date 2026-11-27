#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/capability/external_link.h>

#include "k210_external_link_peer.h"

static const char *outcome(hk_result_t result)
{
    switch(result)
    {
    case HK_OK: return "success";
    case HK_PENDING: return "pending";
    case HK_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case HK_ERR_VERSION_INCOMPATIBLE: return "incompatible_version";
    case HK_ERR_BUSY: return "conflict";
    case HK_ERR_STALE_HANDLE: return "stale";
    case HK_ERR_DEADLINE_EXCEEDED: return "deadline";
    case HK_ERR_CANCELLED: return "cancelled";
    case HK_ERR_INTERNAL: return "internal";
    default: return "unexpected_result";
    }
}

int hk_test_k210_link_trace(int argc, char **argv)
{
    const hk_deadline_t deadline = {1000};
    const hk_external_link_service_t *service = hk_external_link_service();
    hk_external_link_t session = {0};
    hk_external_link_t competitor = {0};
    hk_external_link_t copied = {0};
    hk_external_link_op_t current = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_t saved = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_t other = HK_EXTERNAL_LINK_OP_NONE;
    uint8_t rx[8];
    const uint8_t tx[70] = {1};
    const hk_buffer_view_t uart_tx = {(void *)tx, sizeof(tx), 0, HK_BUFFER_ACCESS_READABLE};
    const hk_external_link_uart_config_t uart = {
        sizeof(uart), HK_EXTERNAL_LINK_UART_CONFIG_VERSION, 115200, 0,
    };
    const hk_external_link_i2c_controller_config_t controller = {
        sizeof(controller), HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION, 100000, 0,
    };
    const hk_external_link_i2c_transfer_t i2c = {
        sizeof(i2c), HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION, 0x42, 0,
        {(void *)tx, 1, 0, HK_BUFFER_ACCESS_READABLE},
        {rx, sizeof(rx), 0, HK_BUFFER_ACCESS_WRITABLE}, 0,
    };
    memset(rx, 0xa5, sizeof(rx));
    hk_test_k210_link_set_now(100);

    for(int i = 1; i < argc; ++i)
    {
        const char *action = argv[i];
        hk_external_link_op_progress_t progress = {0};
        hk_result_t result = HK_OK;
        if(strcmp(action, "open") == 0)
            result = hk_external_link_open(service, HK_EXTERNAL_LINK_FEATURES_0_1, &session);
        else if(strcmp(action, "open-competitor") == 0)
            result = hk_external_link_open(service, HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER, &competitor);
        else if(strcmp(action, "configure-uart") == 0)
            result = hk_external_link_configure_uart(&session, &uart);
        else if(strcmp(action, "configure-i2c") == 0)
            result = hk_external_link_configure_i2c_controller(&session, &controller);
        else if(strcmp(action, "configure-invalid-version") == 0)
        {
            hk_external_link_uart_config_t invalid = uart;
            invalid.struct_version++;
            result = hk_external_link_configure_uart(&session, &invalid);
        }
        else if(strcmp(action, "configure-invalid-baud") == 0)
        {
            hk_external_link_uart_config_t invalid = uart;
            invalid.baud = 1;
            result = hk_external_link_configure_uart(&session, &invalid);
        }
        else if(strcmp(action, "copy-claim") == 0)
            copied = session;
        else if(strcmp(action, "configure-copy") == 0)
            result = hk_external_link_configure_uart(&copied, &uart);
        else if(strcmp(action, "begin-invalid-borrow") == 0)
        {
            hk_buffer_view_t invalid = uart_tx;
            invalid.flags = HK_BUFFER_ACCESS_WRITABLE;
            result = hk_external_link_uart_write_begin(&session, &invalid, deadline, NULL, &other);
        }
        else if(strcmp(action, "begin-uart") == 0)
            result = hk_external_link_uart_write_begin(&session, &uart_tx, deadline, NULL, &current);
        else if(strcmp(action, "begin-i2c") == 0)
            result = hk_external_link_i2c_transfer_begin(&session, &i2c, deadline, NULL, &current);
        else if(strcmp(action, "begin-again-i2c") == 0)
            result = hk_external_link_i2c_transfer_begin(&session, &i2c, deadline, NULL, &other);
        else if(strcmp(action, "save-token") == 0)
            saved = current;
        else if(strcmp(action, "poll") == 0)
            result = hk_external_link_poll(&session, &current, &progress);
        else if(strcmp(action, "poll-saved") == 0)
            result = hk_external_link_poll(&session, &saved, &progress);
        else if(strcmp(action, "cancel") == 0)
            result = hk_external_link_cancel(&session, &current, &progress);
        else if(strcmp(action, "close") == 0)
            result = hk_external_link_close(&session, deadline);
        else if(strcmp(action, "retire") == 0)
            result = hk_external_link_retire(&session, deadline);
        else if(strcmp(action, "expire") == 0)
            hk_test_k210_link_set_now(deadline.at_us);
        else if(strcmp(action, "hold-rx") == 0)
            hk_test_k210_link_hold_rx();
        else if(strcmp(action, "peer-completes") == 0)
            hk_test_k210_link_complete_rx();
        else
            return 2;

        hk_test_k210_link_peer_t peer = hk_test_k210_link_observe();
        unsigned int changed_rx = 0;
        for(size_t index = 0; index < sizeof(rx); ++index)
            changed_rx += rx[index] != 0xa5;
        printf("{\"event\":\"external_link\",\"action\":\"%s\",\"outcome\":\"%s\","
               "\"route_calls\":%" PRIu32 ",\"wire_tx\":%" PRIu32 ",\"late_writes\":%" PRIu32 ","
               "\"pending_rx\":%s,\"rx_changed\":%u,\"tx_progress\":%" PRIu32 ","
               "\"rx_progress\":%" PRIu32 ",\"token\":%" PRIu32 ",\"live_claim\":%s}\n",
               action, outcome(result), peer.route_calls, peer.uart_tx_bytes, peer.late_rx_writes,
               peer.pending_rx ? "true" : "false", changed_rx, progress.tx_completed_bytes,
               progress.rx_completed_bytes, current.generation, session.service ? "true" : "false");
    }
    return 0;
}
