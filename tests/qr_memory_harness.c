#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union { max_align_t alignment; size_t bytes; } allocation_header_t;
static size_t live_bytes;
static int allocations_before_failure = -1;

static void *tracked_malloc(size_t bytes)
{
    allocation_header_t *header;
    if(allocations_before_failure == 0) return NULL;
    if(allocations_before_failure > 0) allocations_before_failure--;
    header = malloc(sizeof(*header) + bytes);
    if(!header) return NULL;
    header->bytes = bytes;
    live_bytes += bytes;
    return header + 1;
}

static void *tracked_calloc(size_t count, size_t bytes)
{
    void *memory = tracked_malloc(count * bytes);
    if(memory) memset(memory, 0, count * bytes);
    return memory;
}

static void tracked_free(void *memory)
{
    if(memory)
    {
        allocation_header_t *header = (allocation_header_t *)memory - 1;
        live_bytes -= header->bytes;
        free(header);
    }
}

/* Exercise the real quirc allocation graph, including partial resize failure. */
#define malloc tracked_malloc
#define calloc tracked_calloc
#define free tracked_free
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../firmware/third_party/quirc/quirc.c"
#pragma GCC diagnostic pop
#undef malloc
#undef calloc
#undef free
#include "../firmware/src/apps/qr_camera/qr_decoder_engine.c"

void qr_result_set_status(const char *status) { (void)status; }
void qr_result_set_payload(const uint8_t *payload, uint16_t size) { (void)payload; (void)size; }
void qr_result_clear_payload(void) {}
const char *qr_result_payload_text(void) { return ""; }
uint8_t *quirc_begin(struct quirc *q, int *width, int *height)
{ (void)q; (void)width; (void)height; abort(); }
void quirc_end(struct quirc *q) { (void)q; abort(); }
void quirc_extract(const struct quirc *q, int index, struct quirc_code *code)
{ (void)q; (void)index; (void)code; abort(); }
quirc_decode_error_t quirc_decode(const struct quirc_code *code, struct quirc_data *data)
{ (void)code; (void)data; abort(); }
void quirc_flip(struct quirc_code *code) { (void)code; abort(); }

int main(void)
{
    qr_engine_stats_t stats;
    qr_decoder_engine_close();
    for(unsigned cycle = 0; cycle < 10; cycle++)
    {
        assert(qr_decoder_init(320, 240));
        assert(live_bytes > 76800);
        assert(qr_decoder_init(640, 480));
        qr_decoder_engine_close();
        assert(live_bytes == 0);
        qr_decoder_engine_close();
        assert(live_bytes == 0);
        qr_decoder_engine_stats(&stats);
        assert(!stats.decoder_ready && !stats.decode_count && !stats.code_count);
    }
    allocations_before_failure = 2;
    assert(!qr_decoder_init(320, 240));
    qr_decoder_engine_close();
    assert(live_bytes == 0);
    allocations_before_failure = -1;
    assert(qr_decoder_init(320, 240));
    qr_decoder_engine_close();
    assert(live_bytes == 0);
    puts("QR_MEMORY_OK");
    return 0;
}
