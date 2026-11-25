#include "../firmware/src/services/external_link_scan.h"

void external_link_scan_start(external_link_scan_t *scan)
{
    *scan = (external_link_scan_t){.result = HK_PENDING};
}

void external_link_scan_stop(external_link_scan_t *scan)
{
    if(scan->result == HK_PENDING)
        scan->result = HK_ERR_CANCELLED;
}

uint8_t external_link_scan_step(external_link_scan_t *scan)
{
    if(scan->result != HK_PENDING)
        return 0U;
    scan->result = HK_OK;
    scan->count = 16U;
    return 1U;
}

uint8_t external_link_scan_address(const external_link_scan_t *scan, uint8_t index)
{
    return index < scan->count ? 0x42U + index : 0U;
}
