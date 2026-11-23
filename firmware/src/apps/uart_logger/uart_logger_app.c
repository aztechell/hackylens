#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/app/data.h>
#include <hackylens/app/runtime.h>

#include "uart_logger_core.h"

enum { WARMUP = 100, STREAM_SAMPLES = 1600, READ_BYTES = 32, READ_STEPS = 8 };

typedef enum
{
    REC_IDLE,
    REC_OPENING,
    REC_RECORD,
    REC_SAVING,
    REC_SAVED,
    REC_ERROR,
} recorder_state_t;

static const char *const s_states[] = {"IDLE", "OPENING", "RECORD", "SAVING", "SAVED", "SD ERROR"};

typedef struct
{
    logger_sample_t queue[UART_LOGGER_QUEUE];
    char block[HK_APP_DATA_CHUNK_MAX];
    char name[HK_APP_DATA_NAME_MAX + 1U];
    uint16_t size, offset;
    uint8_t head, count, header, stop_requested;
    recorder_state_t state;
} logger_recorder_t;

typedef struct
{
    logger_stream_t stream;
    logger_recorder_t recorder;
    hk_external_link_t *link;
    const hk_time_t *time;
    uint64_t render_due;
    uint32_t link_errors;
} logger_app_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_storage[1024];
_Static_assert(sizeof(logger_app_t) <= sizeof(s_storage), "logger exceeds the runtime state slot");

static void cancel_recording(const hk_app_context_t *ctx, logger_recorder_t *recorder)
{
    (void)hk_app_data_cancel(ctx);
    recorder->state = REC_ERROR;
}

static uint8_t recording(const logger_recorder_t *recorder)
{
    return recorder->state >= REC_OPENING && recorder->state <= REC_SAVING;
}

static void start_recording(logger_recorder_t *recorder, uint64_t now)
{
    unsigned number = (unsigned)((now / 1000U) % 1000000U);
    snprintf(recorder->name, sizeof(recorder->name), "TL%06u.CSV", number);
    recorder->state = REC_OPENING;
    recorder->head = recorder->count = 0U;
    recorder->offset = recorder->size = 0U;
    recorder->header = 1U;
    recorder->stop_requested = 0U;
}

static void stop_recording(logger_recorder_t *recorder)
{
    recorder->stop_requested = 1U;
    if(recorder->state == REC_RECORD)
        recorder->state = REC_SAVING;
}

static void enqueue(const hk_app_context_t *ctx, logger_recorder_t *recorder,
                    logger_sample_t sample)
{
    if(recorder->stop_requested || (recorder->state != REC_OPENING && recorder->state != REC_RECORD))
        return;
    if(recorder->count == UART_LOGGER_QUEUE)
    {
        cancel_recording(ctx, recorder);
        return;
    }
    unsigned index = (recorder->head + recorder->count) % UART_LOGGER_QUEUE;
    recorder->queue[index] = sample;
    recorder->count++;
}

static void next_block(logger_recorder_t *recorder)
{
    static const char header[] = "seq,value\n";
    recorder->offset = recorder->size = 0U;
    if(recorder->header)
    {
        memcpy(recorder->block, header, sizeof(header) - 1U);
        recorder->size = sizeof(header) - 1U;
        recorder->header = 0U;
    }
    for(uint8_t row = 0U; row < 16U && recorder->count; ++row)
    {
        recorder->size += logger_csv_row(recorder->block + recorder->size,
            sizeof(recorder->block) - recorder->size, recorder->queue[recorder->head]);
        recorder->head = (recorder->head + 1U) % UART_LOGGER_QUEUE;
        recorder->count--;
    }
}

static void recording_step(const hk_app_context_t *ctx, logger_recorder_t *recorder)
{
    hk_result_t result = HK_OK;
    if(recorder->state == REC_OPENING)
    {
        result = hk_app_data_begin(ctx, recorder->name, strlen(recorder->name));
        if(result == HK_OK)
            recorder->state = recorder->stop_requested ? REC_SAVING : REC_RECORD;
    }
    else if(recorder->state == REC_RECORD || recorder->state == REC_SAVING)
    {
        if(recorder->offset == recorder->size && (recorder->count >= 16U || recorder->stop_requested))
            next_block(recorder);
        if(recorder->offset < recorder->size)
        {
            size_t accepted;
            result = hk_app_data_write(ctx, (const uint8_t *)recorder->block + recorder->offset,
                                       recorder->size - recorder->offset, &accepted);
            recorder->offset += accepted;
        }
        else if(recorder->stop_requested)
        {
            result = hk_app_data_finish(ctx);
            if(result == HK_OK)
                recorder->state = REC_SAVED;
        }
    }
    if(result < 0)
        cancel_recording(ctx, recorder);
}

#ifdef HK_UART_LOGGER_BENCH
enum { BENCH_PLAY, BENCH_GRAPH, BENCH_RECORD, BENCH_PARSE, MARK_CAPACITY = 320 };
static int s_next_mode;
static struct
{
    struct { uint32_t begin, sample; uint16_t seq; } marks[MARK_CAPACITY];
    uint64_t origin, first_sample, last_sample;
    uint32_t measured;
    uint16_t count;
    uint8_t mode, ready, input_done, reported;
} s_bench;

static hk_result_t run_parser_probe(logger_app_t *app)
{
    static uint8_t samples[4096U][UART_LOGGER_PACKET];
    for(unsigned i = 0; i < 4096U; ++i)
        logger_encode(samples[i], (logger_sample_t){i, ((int)(i % 201U) - 100) * 10});

    uint64_t start, stop;
    hk_result_t result = hk_time_now_us(app->time, &start);
    if(result == HK_OK)
    {
        logger_sample_t sample;
        for(unsigned i = 0; i < 4096U; ++i)
            for(unsigned j = 0; j < UART_LOGGER_PACKET; ++j)
                (void)logger_feed(&app->stream, samples[i][j], &sample);
        result = hk_time_now_us(app->time, &stop);
        if(result == HK_OK)
            printf("UL PARSE %lu %lu %llu\r\n", (unsigned long)app->stream.accepted,
                   (unsigned long)app->stream.digest, (unsigned long long)(stop - start));
    }
    return result;
}

static uint8_t measure_sample(logger_app_t *app, uint16_t seq)
{
    if(!s_bench.mode)
        return 1U;
    if(hk_time_now_us(app->time, &s_bench.last_sample) == HK_OK && !s_bench.first_sample)
        s_bench.first_sample = s_bench.last_sample;
    if(!s_bench.ready || seq < WARMUP || seq >= STREAM_SAMPLES)
        return 0U;
    s_bench.measured++;
    return 1U;
}

static void report_run(const logger_app_t *app)
{
    const logger_stream_t *stream = &app->stream;
    const logger_recorder_t *recorder = &app->recorder;
    printf("UL RESULT %lu %lu %lu %lu %lu %lu %lu %s %s\r\n", (unsigned long)stream->accepted,
           (unsigned long)s_bench.measured, (unsigned long)stream->gaps,
           (unsigned long)stream->crc_errors, (unsigned long)stream->stale,
           (unsigned long)app->link_errors, (unsigned long)stream->digest,
           s_states[recorder->state], recorder->name[0] ? recorder->name : "-");
    printf("UL END %u\r\n", (unsigned)s_bench.count);
    s_bench.reported = 1U;
}
#endif

static void read_wire(const hk_app_context_t *ctx, logger_app_t *app)
{
    uint8_t data[READ_BYTES];
    for(uint8_t step = 0U; step < READ_STEPS; ++step)
    {
        hk_buffer_view_t buffer = {data, sizeof(data), 0U, HK_BUFFER_ACCESS_WRITABLE};
        uint32_t received;
        if(hk_external_link_uart_read(app->link, &buffer, &received) != HK_OK)
        {
            app->stream.used = 0U;
            app->link_errors++;
            break;
        }
        for(uint32_t i = 0U; i < received; ++i)
        {
            logger_sample_t sample;
            if(!logger_feed(&app->stream, data[i], &sample))
                continue;
#ifdef HK_UART_LOGGER_BENCH
            if(!measure_sample(app, sample.seq))
                continue;
#endif
            enqueue(ctx, &app->recorder, sample);
        }
        if(received < READ_BYTES)
            break;
    }
}

static hk_result_t logger_start(const hk_app_context_t *ctx)
{
    logger_app_t *app = (void *)s_storage;
    *app = (logger_app_t){0};
    logger_stream_init(&app->stream);
    hk_result_t result = hk_app_context_time(ctx, &app->time);
    if(result != HK_OK)
        return result;
    result = hk_time_now_us(app->time, &app->render_due);
    if(result != HK_OK)
        return result;

#ifdef HK_UART_LOGGER_BENCH
    s_bench.mode = s_next_mode;
    s_next_mode = BENCH_PLAY;
    s_bench.count = s_bench.measured = 0U;
    s_bench.ready = s_bench.input_done = s_bench.reported = 0U;
    s_bench.origin = app->render_due;
    s_bench.first_sample = s_bench.last_sample = 0U;
    if(s_bench.mode == BENCH_PARSE)
        return run_parser_probe(app);
    if(s_bench.mode == BENCH_RECORD)
        start_recording(&app->recorder, app->render_due);
#endif

    result = hk_app_context_external_link(ctx, HK_EXTERNAL_LINK_FEATURE_UART, &app->link);
    if(result != HK_OK)
        return result;
    const hk_external_link_uart_config_t config = {
        sizeof(config), HK_EXTERNAL_LINK_UART_CONFIG_VERSION, 115200U, 0U,
    };
    return hk_external_link_configure_uart(app->link, &config);
}

static hk_result_t logger_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    logger_app_t *app = (void *)s_storage;
    logger_recorder_t *recorder = &app->recorder;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        uint32_t pressed = event->data.input.pressed;
        if(pressed & HK_INPUT_BUTTON_BACK)
            return hk_app_context_request_close(ctx);
        if(pressed & HK_INPUT_BUTTON_OK)
        {
            if(recording(recorder))
                stop_recording(recorder);
            else
                start_recording(recorder, event->timestamp_us);
        }
    }
    if(event->kind == HK_APP_EVENT_MEDIA && event->data.media.kind == HK_APP_MEDIA_REMOVED && recording(recorder))
        cancel_recording(ctx, recorder);
    if(event->kind != HK_APP_EVENT_TIMER || !app->link)
        return HK_OK;

    read_wire(ctx, app);
    uint64_t now = event->data.timer.now_us;
#ifdef HK_UART_LOGGER_BENCH
    if(s_bench.mode && s_bench.first_sample &&
       (app->stream.last_seq == STREAM_SAMPLES - 1 ||
        (now >= s_bench.first_sample && now - s_bench.first_sample >= 33500000U)))
    {
        s_bench.input_done = 1U;
        if(recording(recorder))
            stop_recording(recorder);
    }
#endif
    recording_step(ctx, recorder);
#ifdef HK_UART_LOGGER_BENCH
    if(s_bench.mode && !s_bench.ready && recorder->state != REC_OPENING)
    {
        if(s_bench.mode == BENCH_RECORD && recorder->state != REC_RECORD)
            return HK_ERR_IO;
        s_bench.ready = 1U;
        printf("UL READY %llu\r\n", (unsigned long long)s_bench.origin);
    }
    if(s_bench.input_done)
    {
        if(!s_bench.reported && !recording(recorder))
            report_run(app);
        return HK_OK;
    }
#endif
    if(now >= app->render_due)
    {
        app->render_due = now + 100000U;
        return hk_app_context_request_render(ctx, NULL);
    }
    return HK_OK;
}

static hk_result_t draw_text(hk_app_surface_t *surface, uint16_t x, uint16_t y, const char *text)
{
    const hk_display_rect_t area = {x, y, 216U, 24U};
    return hk_app_surface_text(surface, &area, text, strlen(text), 0xffffU);
}

static hk_result_t logger_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    logger_app_t *app = (void *)s_storage;
    if(!app->link)
        return HK_OK;
    hk_display_info_t info;
    hk_result_t result = hk_app_surface_get_info(surface, &info);
    if(result != HK_OK)
        return result;
    const uint16_t left = (info.width - 240U) / 2U;
    const uint16_t top = (info.height - 240U) / 2U;
    result = hk_app_surface_clear(surface, 0U);
    uint8_t count = logger_rect_count(&app->stream);
    for(uint8_t i = 0U; result == HK_OK && i < count; ++i)
    {
        logger_rect_t rect = logger_rect(&app->stream, i);
        hk_display_rect_t area = {left + rect.x, top + rect.y, rect.width, rect.height};
        result = hk_app_surface_fill_rect(surface, &area, rect.color);
    }
    char text[32] = "WAITING FOR UART";
    if(app->stream.accepted)
        snprintf(text, sizeof(text), "%d LOST %lu", (int)app->stream.last_value,
                 (unsigned long)app->stream.gaps);
    if(result == HK_OK) result = draw_text(surface, left + 20U, top + 7U, "UART LOGGER");
    if(result == HK_OK) result = draw_text(surface, left + 20U, top + 188U, text);
    if(result == HK_OK) result = draw_text(surface, left + 20U, top + 213U, s_states[app->recorder.state]);

#ifdef HK_UART_LOGGER_BENCH
    if(result == HK_OK && s_bench.mode && s_bench.measured && s_bench.count < MARK_CAPACITY)
    {
        uint64_t now;
        result = hk_time_now_us(app->time, &now);
        if(result == HK_OK)
        {
            uint16_t i = s_bench.count++;
            s_bench.marks[i].begin = (uint32_t)(now - s_bench.origin);
            s_bench.marks[i].sample = (uint32_t)(s_bench.last_sample - s_bench.origin);
            s_bench.marks[i].seq = app->stream.last_seq;
        }
    }
#endif
    return result;
}

static hk_result_t logger_stop(const hk_app_context_t *ctx)
{
    return hk_app_data_cancel(ctx);
}

uint8_t logger_debug(const char *command)
{
#ifdef HK_UART_LOGGER_BENCH
    if(strncmp(command, "HKULMODE ", 9U) == 0)
    {
        const char *mode = command + 9U;
        if(strcmp(mode, "GRAPH") == 0) s_next_mode = BENCH_GRAPH;
        else if(strcmp(mode, "RECORD") == 0) s_next_mode = BENCH_RECORD;
        else if(strcmp(mode, "PARSE") == 0) s_next_mode = BENCH_PARSE;
        else { puts("UL MODE ERROR"); return 1U; }
        puts("UL MODE OK");
        return 1U;
    }
    if(strncmp(command, "HKULMARK ", 9U) == 0)
    {
        unsigned long first = strtoul(command + 9U, NULL, 10);
        for(unsigned long i = first; i < s_bench.count && i - first < 8U; ++i)
            printf("UL M %lu %lu %lu %u\r\n", i, (unsigned long)s_bench.marks[i].begin,
                   (unsigned long)s_bench.marks[i].sample, (unsigned)s_bench.marks[i].seq);
        puts("UL MARK END");
        return 1U;
    }
#else
    (void)command;
#endif
    return 0U;
}

const hk_app_v2_entry_t logger_v2_entry = {
    .state_storage = s_storage,
    .state_capacity_bytes = sizeof(s_storage),
    .start = logger_start,
    .event = logger_event,
    .render = logger_render,
    .stop = logger_stop,
};
