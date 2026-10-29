#ifndef HK_SHARED_BUFFER_H
#define HK_SHARED_BUFFER_H

/* Return the address used to share SRAM data with DMA or the other core. */
void *hk_shared_buffer_uncached(void *pointer);

#endif
