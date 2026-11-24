#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/app.h>
#include <hackylens/app/kibble.h>
#include <hackylens/app/panel.h>

enum { BODY_TOP = 32, GLYPH_W = 14, GLYPH_H = 24, GENERATION_SEED = 210, OK_HOLD_US = 700000 };
enum { SCROLL_REPEAT_INITIAL_US = 500000, SCROLL_REPEAT_NEXT_US = 180000 };
enum { TEXT_BYTES = HK_KIBBLE_PROMPT_MAX + HK_KIBBLE_TOKEN_MAX * HK_KIBBLE_GENERATION_MAX + 1 };
#define FOLLOW_TAIL UINT16_MAX

typedef enum { LOADING, READY, GENERATING, COMPLETE, ERROR } kibble_phase_t;

typedef struct
{
    const hk_time_t *clock;
    const char *status;
    uint64_t started, first_us, elapsed_us;
    uint64_t ok_hold_due_us, scroll_due_us;
    uint32_t accelerator_us, head_us;
    uint16_t text_bytes, prompt_bytes;
    uint16_t width, height;
    uint16_t first_line, last_first_line;
    uint8_t prompt, tokens, queued, full, ok_can_start, scroll_button;
    kibble_phase_t phase;
    char custom[HK_KIBBLE_PROMPT_MAX + 1];
    char text[TEXT_BYTES];
} kibble_app_t;

static const char *const prompts[] = {
    "Once upon a time", "A little dog", "One sunny day", "In a small town",
};
enum { PROMPT_COUNT = sizeof(prompts) / sizeof(prompts[0]) };
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t storage[1024];
static kibble_app_t *active;
_Static_assert(sizeof(kibble_app_t) <= sizeof(storage), "KibbleLM runtime state");

static uint64_t now_us(const kibble_app_t *app)
{
    uint64_t now = 0;
    (void)hk_time_now_us(app->clock, &now);
    return now;
}

static hk_result_t redraw(const hk_app_context_t *ctx, const kibble_app_t *app)
{
    if(app->full || !app->width || app->height <= BODY_TOP + 2U)
        return hk_app_context_request_render(ctx, NULL);
    hk_display_rect_t body = {2U, BODY_TOP, app->width - 4U, app->height - BODY_TOP - 2U};
    return hk_app_context_request_render(ctx, &body);
}

static void reset_output(kibble_app_t *app, const char *prompt)
{
    strcpy(app->text, prompt);
    app->text_bytes = app->prompt_bytes = (uint16_t)strlen(prompt);
    app->first_line = FOLLOW_TAIL;
    app->last_first_line = 0U;
    app->scroll_button = 0U;
    app->started = app->first_us = app->elapsed_us = 0U;
    app->accelerator_us = app->head_us = app->tokens = 0U;
}

static void finish_at_word_boundary(kibble_app_t *app)
{
    /* At the token limit, a trailing word may still need another subword. */
    uint16_t end = app->text_bytes;
    while(end > app->prompt_bytes)
    {
        unsigned char ch = (unsigned char)app->text[end - 1U];
        if(!isalnum(ch) && ch != '\'' && ch != '-')
            break;
        --end;
    }
    while(end > app->prompt_bytes && app->text[end - 1U] == ' ')
        --end;
    app->text[end] = '\0';
    app->text_bytes = end;
}

static hk_result_t clear_output(const hk_app_context_t *ctx, kibble_app_t *app)
{
    app->ok_hold_due_us = 0U;
    app->queued = 0U;
    app->custom[0] = '\0';
    reset_output(app, prompts[app->prompt]);
    if(app->phase == GENERATING)
    {
        (void)hk_app_kibble_close(ctx, (hk_deadline_t){ now_us(app) + 250000U });
        app->phase = LOADING;
        app->status = "Loading model";
    }
    else if(app->phase == COMPLETE)
    {
        app->phase = READY;
        app->status = "Choose a prompt";
    }
    return redraw(ctx, app);
}

static hk_result_t start_generation(const hk_app_context_t *ctx, kibble_app_t *app)
{
    const char *prompt = app->custom[0] ? app->custom : prompts[app->prompt];
    reset_output(app, prompt);
    uint32_t seed = app->queued ? GENERATION_SEED : (uint32_t)now_us(app);
    if(hk_app_kibble_start(ctx, prompt, seed) != HK_OK)
    {
        app->phase = ERROR;
        app->status = "Cannot start";
        (void)hk_app_kibble_close(ctx, (hk_deadline_t){ now_us(app) + 250000U });
    }
    else
    {
        app->started = now_us(app);
        app->phase = GENERATING;
        app->status = "Generating";
    }
    app->queued = 0U;
    return redraw(ctx, app);
}

static hk_result_t kibble_lm_start(const hk_app_context_t *ctx)
{
    void *state;
    uint32_t capacity;
    hk_result_t result = hk_app_context_state(ctx, &state, &capacity);
    if(result != HK_OK || capacity < sizeof(kibble_app_t))
        return result != HK_OK ? result : HK_ERR_LIMIT;
    kibble_app_t *app = state;
    *app = (kibble_app_t){ .phase = LOADING, .status = "Loading model", .full = 1U };
    result = hk_app_context_time(ctx, &app->clock);
    if(result != HK_OK)
        return result;
    active = app;
    reset_output(app, prompts[0]);
    return HK_OK;
}

static uint8_t shows_story(const kibble_app_t *app)
{
    return app->phase == GENERATING || app->phase == COMPLETE || app->tokens;
}

static hk_result_t navigate(const hk_app_context_t *ctx, kibble_app_t *app, uint8_t forward)
{
    if(shows_story(app))
    {
        uint16_t line = app->first_line == FOLLOW_TAIL ? app->last_first_line : app->first_line;
        if(forward ? line >= app->last_first_line : line == 0U)
            return HK_OK;
        line = (uint16_t)(forward ? line + 1U : line - 1U);
        app->first_line = line == app->last_first_line ? FOLLOW_TAIL : line;
    }
    else
    {
        unsigned delta = forward ? 1U : PROMPT_COUNT - 1U;
        app->prompt = (uint8_t)((app->prompt + delta) % PROMPT_COUNT);
        app->custom[0] = '\0';
        reset_output(app, prompts[app->prompt]);
    }
    return redraw(ctx, app);
}

static hk_result_t on_timer(const hk_app_context_t *ctx, kibble_app_t *app, uint64_t now)
{
    if(app->ok_hold_due_us && now >= app->ok_hold_due_us)
        return clear_output(ctx, app);
    if(app->scroll_button && now >= app->scroll_due_us)
    {
        app->scroll_due_us = now + SCROLL_REPEAT_NEXT_US;
        hk_result_t result = navigate(ctx, app, app->scroll_button == HK_INPUT_BUTTON_RIGHT);
        if(result != HK_OK)
            return result;
    }
    if(app->phase == LOADING)
    {
        hk_result_t result = hk_app_kibble_open_step(ctx, &app->status);
        if(result == HK_PENDING)
            return HK_OK;
        app->phase = result == HK_OK ? READY : ERROR;
        if(result == HK_OK)
            app->status = "Choose a prompt";
        return redraw(ctx, app);
    }
    if(app->queued && (app->phase == READY || app->phase == COMPLETE))
        return start_generation(ctx, app);
    if(app->phase != GENERATING)
        return HK_OK;

    hk_kibble_token_t token;
    hk_result_t result = hk_app_kibble_poll(ctx, &token);
    if(result == HK_PENDING)
        return HK_OK;
    if(result != HK_OK)
    {
        (void)hk_app_kibble_close(ctx, (hk_deadline_t){ now_us(app) + 250000U });
        app->phase = ERROR;
        app->status = "Inference failed";
        return redraw(ctx, app);
    }

    app->elapsed_us = now_us(app) - app->started;
    app->accelerator_us += token.accelerator_us;
    app->head_us += token.cpu_us;
    if(!token.end)
    {
        if(!app->tokens)
            app->first_us = app->elapsed_us;
        size_t length = strlen(token.text);
        memcpy(app->text + app->text_bytes, token.text, length + 1U);
        app->text_bytes += (uint16_t)length;
        ++app->tokens;
    }
    if(token.end || app->tokens == HK_KIBBLE_GENERATION_MAX)
    {
        if(!token.end)
            finish_at_word_boundary(app);
        app->phase = COMPLETE;
        app->status = "Done";
    }
    return redraw(ctx, app);
}

static hk_result_t kibble_lm_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    kibble_app_t *app = active;
    if(event->kind == HK_APP_EVENT_TIMER)
        return on_timer(ctx, app, event->timestamp_us);
    if(event->kind == HK_APP_EVENT_MEDIA)
    {
        if(event->data.media.kind == HK_APP_MEDIA_REMOVED || event->data.media.kind == HK_APP_MEDIA_ERROR)
        {
            (void)hk_app_kibble_close(ctx, (hk_deadline_t){ now_us(app) + 250000U });
            app->phase = ERROR;
            app->status = "Insert model SD";
            app->queued = 0U;
            app->ok_hold_due_us = 0U;
            app->scroll_button = 0U;
            return redraw(ctx, app);
        }
        if(app->phase == ERROR)
            app->phase = LOADING;
        return HK_OK;
    }
    if(event->kind != HK_APP_EVENT_INPUT)
        return HK_OK;
    const hk_input_event_t *input = &event->data.input;
    uint32_t buttons = input->pressed;
    if(buttons & HK_INPUT_BUTTON_BACK)
        return hk_app_context_request_close(ctx);
    if(input->dropped)
        app->ok_hold_due_us = 0U;
    if(input->dropped || (input->released & app->scroll_button))
        app->scroll_button = 0U;
    if((buttons & HK_INPUT_BUTTON_OK) && app->phase != LOADING)
    {
        app->ok_hold_due_us = event->timestamp_us + OK_HOLD_US;
        app->ok_can_start = app->phase != GENERATING;
    }
    if((input->released & HK_INPUT_BUTTON_OK) && app->ok_hold_due_us)
    {
        uint64_t hold_due = app->ok_hold_due_us;
        app->ok_hold_due_us = 0U;
        if(event->timestamp_us >= hold_due)
            return clear_output(ctx, app);
        if(!app->ok_can_start || app->phase == GENERATING || app->phase == LOADING || app->queued)
            return HK_OK;
        if(app->phase == ERROR)
        {
            app->phase = LOADING;
            return HK_OK;
        }
        return start_generation(ctx, app);
    }
    if(buttons & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_RIGHT))
    {
        uint8_t forward = (buttons & HK_INPUT_BUTTON_RIGHT) != 0U;
        if(shows_story(app))
        {
            app->scroll_button = forward ? HK_INPUT_BUTTON_RIGHT : HK_INPUT_BUTTON_LEFT;
            app->scroll_due_us = event->timestamp_us + SCROLL_REPEAT_INITIAL_US;
        }
        return navigate(ctx, app, forward);
    }
    return HK_OK;
}

static const char *wrap(const char *text, unsigned columns, unsigned *length)
{
    unsigned i = 0, space = 0;
    while(text[i] && i < columns)
    {
        if(text[i] == ' ')
            space = i;
        ++i;
    }
    if(text[i] && text[i] != ' ' && space)
        i = space;
    *length = i;
    while(text[i] == ' ')
        ++i;
    return text + i;
}

static hk_result_t kibble_lm_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    kibble_app_t *app = active;
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    if(panel.result != HK_OK)
        return panel.result;
    app->width = panel.width;
    app->height = panel.height;
    if(app->full)
        hk_app_panel_chrome(&panel, "KibbleLM");
    app->full = 0U;
    if(panel.width < GLYPH_W + 16U || panel.height < BODY_TOP + 2U * GLYPH_H + 4U)
        return panel.result;
    const unsigned status_y = panel.height - GLYPH_H - 4U;
    const unsigned rows = (status_y - BODY_TOP) / GLYPH_H;
    unsigned columns = (panel.width - 16U) / GLYPH_W;
    char line[32];
    if(columns >= sizeof(line))
        columns = sizeof(line) - 1U;
    if(!rows || !columns)
        return panel.result;

    unsigned total = 0, length;
    const char *text = app->text;
    for(const char *p = text; *p; p = wrap(p, columns, &length))
        ++total;
    app->last_first_line = (uint16_t)(total > rows ? total - rows : 0U);
    unsigned first = app->first_line;
    if(first > app->last_first_line)
        first = app->last_first_line;
    if(app->first_line != FOLLOW_TAIL)
        app->first_line = (uint16_t)first;
    for(unsigned i = 0; i < first; ++i)
        text = wrap(text, columns, &length);
    hk_app_panel_fill(&panel, 2, BODY_TOP, panel.width - 4, panel.height - BODY_TOP - 2, HK_PANEL_BLACK);
    for(unsigned row = 0; *text && row < rows; ++row)
    {
        const char *next = wrap(text, columns, &length);
        memcpy(line, text, length);
        line[length] = '\0';
        hk_app_panel_label(&panel, 8, BODY_TOP + row * GLYPH_H, panel.width - 16,
                           line, 0xffffU, HK_PANEL_LEFT);
        text = next;
    }
    const char *status = app->status;
    char position[32];
    if(app->last_first_line && (app->phase == GENERATING || app->phase == COMPLETE))
    {
        snprintf(position, sizeof(position), "%s %u-%u/%u",
                 app->phase == GENERATING ? "Gen" : "Done", first + 1U, first + rows, total);
        status = position;
    }
    hk_app_panel_status(&panel, status_y, status);
    return panel.result;
}

static hk_result_t kibble_lm_stop(const hk_app_context_t *ctx)
{
    active = NULL;
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    return result == HK_OK ? hk_app_kibble_close(ctx, deadline) : result;
}

uint8_t kibble_lm_debug(const char *command)
{
    kibble_app_t *app = active;
    if(strcmp(command, "HKKLINFO") == 0)
    {
        if(!app)
            puts("HKKLINFO active=0");
        else
        {
            printf("HKKLINFO phase=%u tokens=%u first_us=%" PRIu64 " elapsed_us=%" PRIu64
                   " accelerator_us=%" PRIu32 " head_us=%" PRIu32 "\r\n", (unsigned)app->phase,
                   (unsigned)app->tokens, app->first_us, app->elapsed_us, app->accelerator_us, app->head_us);
            printf("HKKL TEXT %s\r\n", app->text);
        }
        return 1U;
    }
    int preset = strcmp(command, "HKKL") == 0;
    if(!preset && strncmp(command, "HKKL ", 5) != 0)
        return 0U;
    const char *prompt = preset ? "" : command + 5;
    size_t length = strlen(prompt);
    if(!app || (app->phase != READY && app->phase != COMPLETE) || (!preset && !length) ||
       length > HK_KIBBLE_PROMPT_MAX)
    {
        puts("HKKL ERROR");
        return 1U;
    }
    for(size_t i = 0; i < length; ++i)
    {
        if((uint8_t)prompt[i] < 32U || (uint8_t)prompt[i] > 126U)
        {
            puts("HKKL ERROR");
            return 1U;
        }
    }
    memcpy(app->custom, prompt, length + 1U);
    app->queued = 1U;
    puts("HKKL OK");
    return 1U;
}

const hk_app_v2_entry_t kibble_lm_v2_entry = {
    .state_storage = storage, .state_capacity_bytes = sizeof(storage),
    .start = kibble_lm_start, .event = kibble_lm_event,
    .render = kibble_lm_render, .stop = kibble_lm_stop,
};
