#include "sleep_app.h"
#include "sleep_controller.h"
#include "sleep_view.h"
#include "sleep_firmware.h"
#include <hackylens/capability/power.h>
#include <hackylens/capability/time.h>
#include <string.h>
#include "power_view_assets.h"

typedef struct {
    sleep_state_t sleep;
    hk_deadline_t deadline;
    uint8_t selected, armed, sleeping, waiting, failed;
} power_state_t;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t power_storage[1024];
static hk_result_t power_state(const hk_app_context_t *ctx, power_state_t **state)
{
    void *data = NULL;
    uint32_t size = 0U;
    hk_result_t result = hk_app_context_state(ctx, &data, &size);
    if(result != HK_OK) return result;
    if(!data || size < sizeof(**state)) return HK_ERR_LIMIT;
    *state = data;
    return HK_OK;
}
static hk_result_t power_start(const hk_app_context_t *ctx)
{
    power_state_t *state;
    const hk_input_t *input;
    uint32_t buttons;
    hk_result_t result = power_state(ctx, &state);
    if(result != HK_OK) return result;
    if(hk_app_context_input(ctx, &input) != HK_OK ||
       hk_input_get_state(input, &buttons) != HK_OK) return HK_ERR_INTERNAL;
    memset(state, 0, sizeof(*state));
    state->armed = !(buttons & HK_INPUT_BUTTON_OK);
    if(auto_sleep_take_request()) {
        state->sleeping = 1U;
        sleep_controller_reset(&state->sleep);
    }
    return HK_OK;
}
static uint8_t power_available(uint8_t selected)
{
    uint32_t features = hk_power_features(hk_power_service());
    return selected == 0U || (features & (selected == 1U ?
        HK_POWER_FEATURE_OFF : HK_POWER_FEATURE_RESTART)) != 0U;
}
static hk_result_t power_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    power_state_t *state;
    hk_result_t result = power_state(ctx, &state);
    if(result != HK_OK) return result;
    if(!event) return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT) {
        uint32_t pressed = event->data.input.pressed;
        if(state->sleeping) {
            sleep_controller_handle_input(&state->sleep, &event->data.input);
            if(state->sleep.close_requested) return hk_app_context_request_close(ctx);
            return HK_OK;
        }
        if(pressed & HK_INPUT_BUTTON_BACK) {
            if(state->waiting) return HK_OK;
            return hk_app_context_request_close(ctx);
        }
        if(event->data.input.released & HK_INPUT_BUTTON_OK) state->armed = 1U;
        if(!state->waiting) {
            if(pressed & HK_INPUT_BUTTON_LEFT) state->selected = (state->selected + 2U) % 3U;
            if(pressed & HK_INPUT_BUTTON_RIGHT) state->selected = (state->selected + 1U) % 3U;
        }
        if(state->armed && !state->waiting && (pressed & HK_INPUT_BUTTON_OK)) {
            state->armed = 0U; /* Every action requires a fresh release/press. */
            if(power_available(state->selected)) {
                if(state->selected == 0U) {
                    state->sleeping = 1U;
                    sleep_controller_reset(&state->sleep);
                } else {
                    result = hk_time_deadline_after_us(hk_time_service(), 2000000U, &state->deadline);
                    if(result != HK_OK) return result;
                    state->waiting = 1U;
                    state->failed = 0U;
                }
            }
        }
        result = hk_app_context_request_render(ctx, NULL);
        if(result != HK_OK) return result;
    }
    if(state->waiting) {
        result = state->selected == 1U ? hk_power_request_off(hk_power_service(), state->deadline) :
            hk_power_request_restart(hk_power_service(), state->deadline);
        if(result != HK_PENDING) {
            state->waiting = 0U;
            state->failed = result != HK_OK;
            if(result == HK_OK) return hk_app_context_request_close(ctx);
        }
        return hk_app_context_request_render(ctx, NULL);
    }
    return HK_OK;
}
static hk_result_t power_rect(hk_display_surface_t *s, int x, int y, unsigned w, unsigned h, uint16_t c)
{
    unsigned px, py;
    if(x<0 || y<0 || (uint64_t)(unsigned)x+w>s->width ||
       (uint64_t)(unsigned)y+h>s->height) return HK_ERR_INVALID_ARGUMENT;
    for(py=(unsigned)y;py<(unsigned)y+h;py++) {
        uint8_t *row=(uint8_t *)s->pixels.data+py*s->pixels.stride_bytes;
        for(px=(unsigned)x;px<(unsigned)x+w;px++) {
            row[px*2U]=(uint8_t)(c>>8);
            row[px*2U+1U]=(uint8_t)c;
        }
    }
    return HK_OK;
}
static hk_result_t power_text_scaled(hk_display_surface_t *s, int x, int y,
    unsigned width, const char *label, uint16_t color, unsigned sample)
{
    unsigned n, gx, gy, advance=14U/sample;
    for(n=0; label[n] && (n+1U)*advance<=width; n++) {
        unsigned code=(unsigned char)label[n];
        const uint8_t *glyph;
        if(code<32U || code>126U) code='?';
        glyph=g_hackylens_font_1bpp+(code-32U)*48U;
        for(gy=0;gy<24U/sample;gy++) for(gx=0;gx<advance;gx++) {
            unsigned sx=gx*sample, sy=gy*sample;
            if(glyph[sy*2U+sx/8U] & (0x80U>>(sx&7U))) {
                hk_result_t result=power_rect(s,x+(int)(n*advance+gx),y+(int)gy,1,1,color);
                if(result!=HK_OK) return result;
            }
        }
    }
    return HK_OK;
}
static hk_result_t power_text(hk_display_surface_t *s, int x, int y,
    unsigned width, const char *label, uint16_t color)
{
    return power_text_scaled(s,x,y,width,label,color,2U);
}
#define DRAW(call) do { hk_result_t r_ = (call); if(r_ != HK_OK) return r_; } while(0)
static hk_result_t power_icon(hk_display_surface_t *s, unsigned i, int x, int y, uint16_t color)
{
    unsigned px, py;
    for(py=0;py<HK_POWER_ICON_SIZE;py++) for(px=0;px<HK_POWER_ICON_SIZE;px++)
        if(hk_power_icon_pixel(i,px,py))
            DRAW(power_rect(s,x+(int)px,y+(int)py,1,1,color));
    return HK_OK;
}
static hk_result_t power_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    static const char *const labels[] = {"SLEEP", "POWER OFF", "RESTART"};
    power_state_t *state;
    hk_display_info_t info;
    hk_display_surface_t pixels;
    unsigned i;
    int block_y;
    hk_result_t result = power_state(ctx, &state);
    if(result != HK_OK) return result;
    if(state->sleeping) return sleep_view_render(surface);
    DRAW(hk_app_surface_get_info(surface, &info));
    /* The command backend holds only 32 draw operations. Borrow its native
     * framebuffer once; font pixels must not each become a queued command. */
    DRAW(hk_app_surface_lock(surface,&pixels));
    if(pixels.pixel_format != HK_DISPLAY_FORMAT_RGB565_BE || !pixels.pixels.data ||
       !(pixels.pixels.flags & HK_BUFFER_ACCESS_WRITABLE) ||
       pixels.pixels.stride_bytes < pixels.width*2U ||
       (uint64_t)pixels.pixels.stride_bytes*pixels.height > pixels.pixels.size_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    DRAW(power_rect(&pixels,0,0,info.width,info.height,0U));
    DRAW(power_rect(&pixels,0,28,info.width,2,0x07e0));
    DRAW(power_text_scaled(&pixels,12,2,info.width-24,"POWER",0x07e0,1U));
    /* 70px icon/label block plus symmetric 8px frame padding. */
    block_y = 30 + ((int)info.height - 30 - 70) / 2;
    for(i=0;i<3U;i++) {
        uint16_t color = power_available(i) ? 0x07e0U : 0x4208U;
        int center = (int)((2U*i+1U)*info.width/6U);
        hk_display_rect_t box = {center-38,block_y-8,76,86};
        if(i == state->selected) {
            DRAW(power_rect(&pixels,box.x,box.y,box.width,1,color));
            DRAW(power_rect(&pixels,box.x,box.y+(int)box.height-1,box.width,1,color));
            DRAW(power_rect(&pixels,box.x,box.y,1,box.height,color));
            DRAW(power_rect(&pixels,box.x+(int)box.width-1,box.y,1,box.height,color));
        }
        DRAW(power_icon(&pixels,i,center-24,block_y,color));
        DRAW(power_text(&pixels,center-(int)strlen(labels[i])*7/2,block_y+58,(unsigned)strlen(labels[i])*7U,labels[i],color));
    }
    if(state->failed || state->waiting || !power_available(state->selected)) {
        const char *status = state->failed ? "FAILED - OK: RETRY" :
            state->waiting ? "STOPPING..." : "NOT SUPPORTED";
        unsigned text_width = (unsigned)strlen(status)*7U;
        DRAW(power_text(&pixels,((int)info.width-(int)text_width)/2,
            (int)info.height-24,text_width,status,0x07e0));
    }
    return hk_app_surface_invalidate(surface,NULL);
}
static hk_result_t power_stop(const hk_app_context_t *ctx)
{
    power_state_t *state;
    hk_deadline_t deadline;
    if(power_state(ctx,&state) == HK_OK && state->sleeping) sleep_controller_exit(&state->sleep);
    return hk_app_context_teardown_deadline(ctx,&deadline);
}
const hk_app_v2_entry_t power_v2_entry = {
    .state_storage=power_storage, .state_capacity_bytes=sizeof(power_storage),
    .start=power_start, .event=power_event, .render=power_render, .stop=power_stop,
};
