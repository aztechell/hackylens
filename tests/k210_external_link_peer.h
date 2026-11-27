#ifndef HK_TEST_K210_EXTERNAL_LINK_PEER_H
#define HK_TEST_K210_EXTERNAL_LINK_PEER_H

#include <stdint.h>

typedef struct
{
    uint32_t route_calls;
    uint32_t uart_tx_bytes;
    uint32_t late_rx_writes;
    uint8_t pending_rx;
} hk_test_k210_link_peer_t;

void hk_test_k210_link_set_now(uint64_t now_us);
void hk_test_k210_link_hold_rx(void);
void hk_test_k210_link_complete_rx(void);
hk_test_k210_link_peer_t hk_test_k210_link_observe(void);
int hk_test_k210_link_trace(int argc, char **argv);

#endif
