#include <assert.h>
#include "board_config.h"
#include "../firmware/assets/hackylens_font_1bpp.h"
#include <stdio.h>
#include <string.h>
#include "../firmware/src/apps/sleep/sleep_app.h"
#include "../firmware/src/services/micropython_runtime.h"
#include "../firmware/src/storage/userfs.h"
#include <hackylens/capability/power.h>
#include "../firmware/src/apps/sleep/sleep_controller.h"
static unsigned restarts, sleeps, wakes;
static unsigned display_width=240;
static uint16_t pixels[240][320];
uint8_t auto_sleep_take_request(void) { return 0U; }
void sleep_controller_reset(sleep_state_t *state) { memset(state,0,sizeof(*state)); sleeps++; }
void sleep_controller_exit(sleep_state_t *state) { (void)state; wakes++; }
void sleep_controller_handle_input(sleep_state_t *state, const hk_input_event_t *event) { if(event->pressed) state->close_requested=1U; }
hk_result_t sleep_view_render(hk_app_surface_t *surface) { (void)surface; return HK_OK; }
void hal_restart(void) { restarts++; }
hk_result_t hk_app_surface_get_info(const hk_app_surface_t *s, hk_display_info_t *info) { (void)s; memset(info,0,sizeof(*info)); info->width=display_width; info->height=240; return HK_OK; }
hk_result_t hk_app_surface_fill_rect(hk_app_surface_t *s,const hk_display_rect_t *r,uint16_t c) {
    unsigned x,y;
    (void)s;
    assert(r->x>=0 && r->y>=0 && r->x+(int)r->width<=(int)display_width && r->y+(int)r->height<=240);
    for(y=r->y;y<(unsigned)r->y+r->height;y++) for(x=r->x;x<(unsigned)r->x+r->width;x++) pixels[y][x]=c;
    return HK_OK;
}
hk_result_t hk_app_surface_stroke_rect(hk_app_surface_t *s,const hk_display_rect_t *r,uint16_t c) {
    hk_display_rect_t edge={r->x,r->y,r->width,1};
    hk_app_surface_fill_rect(s,&edge,c); edge.y+=r->height-1; hk_app_surface_fill_rect(s,&edge,c);
    edge=(hk_display_rect_t){r->x,r->y,1,r->height}; hk_app_surface_fill_rect(s,&edge,c);
    edge.x+=r->width-1; return hk_app_surface_fill_rect(s,&edge,c);
}
hk_result_t hk_app_surface_lock(hk_app_surface_t *s,hk_display_surface_t *out) {
    (void)s; *out=(hk_display_surface_t){.width=display_width,.height=240,.pixel_format=HK_DISPLAY_FORMAT_RGB565_BE,
        .pixels={pixels,sizeof(pixels),640,HK_BUFFER_ACCESS_WRITABLE}}; return HK_OK;
}
hk_result_t hk_app_surface_invalidate(hk_app_surface_t *s,const hk_display_rect_t *r) { (void)s;(void)r;return HK_OK; }
static void preview(unsigned width) {
    unsigned x,y; char name[80]; FILE *f;
    display_width=width; memset(pixels,0,sizeof(pixels));
    assert(power_v2_entry.start(NULL)==HK_OK);
    assert(power_v2_entry.render(NULL,(hk_app_surface_t *)1)==HK_OK);
    snprintf(name,sizeof(name),"build/power-preview-%u"
#if !HK_BOARD_POWER_AXP173
        "-sen"
#endif
        ".ppm",width); f=fopen(name,"wb"); assert(f);
    fprintf(f,"P6\n%u 240\n255\n",width);
    for(y=0;y<240;y++) for(x=0;x<width;x++) {
        uint16_t raw=pixels[y][x]; uint16_t c=(uint16_t)((raw>>8)|(raw<<8));
        fputc(((c>>11)&31)*255/31,f); fputc(((c>>5)&63)*255/63,f); fputc((c&31)*255/31,f);
    }
    fclose(f);
}
static uint64_t now = 100U;
static uint32_t buttons;
static unsigned closes, stop_requests, saves, aborts, unmounts, remounts, off_calls;
static uint8_t save_ok = 1U, off_ok = 1U;
static micropython_runtime_state_t runtime_state;
static userfs_status_t filesystem = {.state = USERFS_STATE_MOUNTED};
uint64_t hal_time_us(void) { return now; }
uint8_t settings_storage_flush(void) { saves++; return save_ok; }
void micropython_runtime_get_status(micropython_runtime_status_t *status)
{ memset(status, 0, sizeof(*status)); status->state = runtime_state; }
uint8_t micropython_runtime_request_stop(void) { stop_requests++; return 1U; }
void userfs_get_status(userfs_status_t *status) { *status = filesystem; }
userfs_result_t userfs_upload_abort(uint32_t id)
{ assert(id == 7U); aborts++; filesystem.upload_id = 0U; return USERFS_OK; }
userfs_result_t userfs_unmount(void)
{ unmounts++; filesystem.state = USERFS_STATE_UNINITIALIZED; return USERFS_OK; }
userfs_result_t userfs_mount(void)
{ remounts++; filesystem.state = USERFS_STATE_MOUNTED; return USERFS_OK; }
uint8_t axp173_power_off(void)
{ assert(filesystem.state != USERFS_STATE_MOUNTED); off_calls++; return off_ok; }
const hk_time_t *hk_time_service(void) { return (const hk_time_t *)1; }
hk_result_t hk_time_deadline_after_us(const hk_time_t *time, uint64_t duration, hk_deadline_t *deadline)
{ (void)time; deadline->at_us = now + duration; return HK_OK; }
hk_result_t hk_app_context_state(const hk_app_context_t *ctx, void **data, uint32_t *size)
{ (void)ctx; *data = power_v2_entry.state_storage; *size = power_v2_entry.state_capacity_bytes; return HK_OK; }
hk_result_t hk_app_context_input(const hk_app_context_t *ctx, const hk_input_t **input)
{ (void)ctx; *input = (const hk_input_t *)1; return HK_OK; }
hk_result_t hk_input_get_state(const hk_input_t *input, uint32_t *state)
{ (void)input; *state = buttons; return HK_OK; }
hk_result_t hk_app_context_request_render(const hk_app_context_t *ctx, const hk_display_rect_t *rect)
{ (void)ctx; (void)rect; return HK_OK; }
hk_result_t hk_app_context_request_close(const hk_app_context_t *ctx)
{ (void)ctx; closes++; return HK_OK; }
hk_result_t hk_app_context_teardown_deadline(const hk_app_context_t *ctx, hk_deadline_t *deadline)
{ (void)ctx; deadline->at_us = now + 100U; return HK_OK; }
hk_result_t hk_app_surface_clear(hk_app_surface_t *surface, uint16_t color)
{ (void)surface; (void)color; return HK_OK; }
hk_result_t hk_app_surface_text(hk_app_surface_t *surface, const hk_display_rect_t *rect,
                              const char *text, uint32_t size, uint16_t color)
{
    unsigned n,x,y;
    assert(rect->x>=0 && rect->x+(int)rect->width<=(int)display_width);
    for(n=0;n<size;n++) for(y=0;y<24;y++) for(x=0;x<14;x++) {
        const uint8_t *glyph=g_hackylens_font_1bpp+((unsigned char)text[n]-32U)*48U;
        if(glyph[y*2+x/8] & (0x80U>>(x&7))) {
            hk_display_rect_t dot={rect->x+(int)(n*14+x),rect->y+(int)y,1,1};
            hk_app_surface_fill_rect(surface,&dot,color);
        }
    }
    return HK_OK;
}
static void input(uint32_t pressed, uint32_t released)
{
    hk_app_event_t event = {.kind = HK_APP_EVENT_INPUT};
    event.data.input.pressed = pressed; event.data.input.released = released;
    assert(power_v2_entry.event(NULL, &event) == HK_OK);
}
static void tick(void)
{ hk_app_event_t event = {.kind = HK_APP_EVENT_TIMER}; assert(power_v2_entry.event(NULL, &event) == HK_OK); }
static void ok(void) { input(0U,HK_INPUT_BUTTON_OK); input(HK_INPUT_BUTTON_OK,0U); }
static void choose_off(void) { input(HK_INPUT_BUTTON_RIGHT,0U); ok(); }
int main(void)
{
#if !HK_BOARD_POWER_AXP173
    buttons=0; assert(power_v2_entry.start(NULL)==HK_OK);
    assert(hk_power_features(hk_power_service())==HK_POWER_FEATURE_RESTART);
    input(HK_INPUT_BUTTON_RIGHT,0U); ok(); assert(!off_calls && !saves);
    assert(hk_power_request_off(hk_power_service(),(hk_deadline_t){0})==HK_ERR_CAPABILITY_ABSENT);
    input(HK_INPUT_BUTTON_RIGHT,0U); ok(); assert(restarts==1 && !off_calls);
    input(HK_INPUT_BUTTON_OK,0U); assert(restarts==1); /* Held OK cannot repeat. */
    preview(240); preview(320);
    puts("POWER_OFF_OK SEN disabled-off restart layout"); return 0;
#endif
    buttons=HK_INPUT_BUTTON_OK;
    assert(power_v2_entry.start(NULL)==HK_OK);
    input(HK_INPUT_BUTTON_RIGHT,0U);
    input(HK_INPUT_BUTTON_OK,0U); assert(!sleeps && !off_calls);
    input(HK_INPUT_BUTTON_BACK,0U); assert(closes==1);
    assert(power_v2_entry.start(NULL)==HK_OK);
    input(HK_INPUT_BUTTON_LEFT,0U);
    input(HK_INPUT_BUTTON_OK,0U); assert(!restarts);
    input(HK_INPUT_BUTTON_BACK,0U); assert(closes==2);
    buttons=0; assert(power_v2_entry.start(NULL)==HK_OK);
    ok(); assert(sleeps==1); input(HK_INPUT_BUTTON_LEFT,0U); assert(closes==3);
    assert(power_v2_entry.stop(NULL)==HK_OK && wakes==1);
    assert(power_v2_entry.start(NULL)==HK_OK);
    input(HK_INPUT_BUTTON_RIGHT,0U);
    input(HK_INPUT_BUTTON_BACK,0U); assert(closes==4 && !off_calls);
    assert(power_v2_entry.start(NULL)==HK_OK);
    input(HK_INPUT_BUTTON_RIGHT,0U); runtime_state=MICROPYTHON_RUNTIME_RUNNING; ok();
    assert(stop_requests==1 && !saves);
    runtime_state=MICROPYTHON_RUNTIME_FINISHED; save_ok=0; tick(); assert(!off_calls);
    input(HK_INPUT_BUTTON_OK,0U); assert(saves==1 && !off_calls); /* Retry needs release. */
    save_ok=1; off_ok=0; filesystem.upload_id=7; ok();
    assert(aborts==1 && off_calls==1 && remounts==1);
    off_ok=1; ok(); assert(off_calls==2); tick(); assert(off_calls==2);
    input(HK_INPUT_BUTTON_OK,0U); assert(off_calls==2);
    assert(power_v2_entry.start(NULL)==HK_OK);
    runtime_state=MICROPYTHON_RUNTIME_RUNNING; choose_off(); now+=2000001U; tick(); assert(off_calls==2);
    assert(power_v2_entry.start(NULL)==HK_OK); runtime_state=MICROPYTHON_RUNTIME_FINISHED;
    input(HK_INPUT_BUTTON_LEFT,0U); ok(); assert(restarts==1 && off_calls==2);
    input(HK_INPUT_BUTTON_OK,0U); assert(restarts==1);
    assert(power_v2_entry.render(NULL,(hk_app_surface_t *)1)==HK_OK);
    assert(hk_power_features(hk_power_service())==(HK_POWER_FEATURE_OFF|HK_POWER_FEATURE_RESTART));
    assert(hk_power_request_off(NULL,(hk_deadline_t){0})==HK_ERR_CAPABILITY_ABSENT);
    preview(240); preview(320);
    puts("POWER_OFF_OK single-press selection held-OK sleep wake restart flush rollback deadline");
    return 0;
}
