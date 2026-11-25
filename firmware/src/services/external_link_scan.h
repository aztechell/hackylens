#ifndef HK_EXTERNAL_LINK_SCAN_H
#define HK_EXTERNAL_LINK_SCAN_H

#include <hackylens/capability/external_link.h>

typedef struct
{
    hk_external_link_t link;
    hk_result_t result;
    uint8_t found[16];
    uint8_t next_address;
    uint8_t count;
} external_link_scan_t;

void external_link_scan_start(external_link_scan_t *scan);
void external_link_scan_stop(external_link_scan_t *scan);
/* Advance a bounded batch. Return nonzero when the scan finishes. */
uint8_t external_link_scan_step(external_link_scan_t *scan);
uint8_t external_link_scan_address(const external_link_scan_t *scan, uint8_t index);

#endif
