/* Real public surface and K210 display backend: transport alone is simulated. */
#include "board_config.h"
#define TEST_WIDTH HK_BOARD_UI_WIDTH
#define main display_contract_main
#include "k210_display_harness.c"
#undef main
#include "../firmware/src/app_runtime/surface_private.h"
#include "../firmware/src/apps/sleep/sleep_app.h"
#include "../firmware/src/apps/sleep/sleep_controller.h"
#include <hackylens/capability/power.h>
#include <assert.h>
#include "../firmware/src/ui/display_binding.h"
#include "../firmware/assets/hackylens_power_icons_1bpp.h"
#include "../firmware/assets/hackylens_menu_icons_1bpp.h"
void power_draw_icon(uint16_t x,uint16_t y,uint16_t c,uint16_t bg);
static hk_result_t action_result=HK_PENDING;

static hk_display_t power_display;
static hk_display_info_t power_info;
static unsigned queued;
static uint8_t locked;
uint8_t auto_sleep_take_request(void) { return 0; }
void sleep_controller_reset(sleep_state_t *s) { memset(s,0,sizeof(*s)); }
void sleep_controller_exit(sleep_state_t *s) { (void)s; }
void sleep_controller_handle_input(sleep_state_t *s,const hk_input_event_t *e) { (void)s;(void)e; }
hk_result_t sleep_view_render(hk_app_surface_t *s) { return hk_app_surface_clear(s,0); }
const hk_power_t *hk_power_service(void) { return (const hk_power_t *)1; }
uint32_t hk_power_features(const hk_power_t *p) { (void)p; return HK_POWER_FEATURE_RESTART | (HK_BOARD_POWER_AXP173 ? HK_POWER_FEATURE_OFF : 0U); }
hk_result_t hk_power_request_off(const hk_power_t *p,hk_deadline_t d) { (void)p;(void)d;return action_result; }
hk_result_t hk_power_request_restart(const hk_power_t *p,hk_deadline_t d) { (void)p;(void)d;return action_result; }
const hk_time_t *hk_time_service(void) { return (const hk_time_t *)1; }
hk_result_t hk_time_deadline_after_us(const hk_time_t *t,uint64_t u,hk_deadline_t *d) { (void)t;d->at_us=s_now_us+u;return HK_OK; }
hk_result_t hk_app_context_state(const hk_app_context_t *c,void **p,uint32_t *n) { (void)c;*p=power_v2_entry.state_storage;*n=power_v2_entry.state_capacity_bytes;return HK_OK; }
hk_result_t hk_app_context_input(const hk_app_context_t *c,const hk_input_t **i) { (void)c;*i=(const hk_input_t *)1;return HK_OK; }
hk_result_t hk_input_get_state(const hk_input_t *i,uint32_t *b) { (void)i;*b=0;return HK_OK; }
hk_result_t hk_app_context_request_render(const hk_app_context_t *c,const hk_display_rect_t *r) { (void)c;(void)r;return HK_OK; }
hk_result_t hk_app_context_request_close(const hk_app_context_t *c) { (void)c;return HK_OK; }
hk_result_t hk_app_context_teardown_deadline(const hk_app_context_t *c,hk_deadline_t *d) { (void)c;d->at_us=500000;return HK_OK; }
static hk_result_t clear(void *u,uint16_t c) { (void)u;queued++;return hk_display_clear(&power_display,c); }
static hk_result_t fill(void *u,const hk_display_rect_t *r,uint16_t c) { (void)u;queued++;return hk_display_fill_rect(&power_display,r,c); }
static hk_result_t stroke(void *u,const hk_display_rect_t *r,uint16_t c) { (void)u;queued++;return hk_display_stroke_rect(&power_display,r,c); }
static hk_result_t text(void *u,const hk_display_rect_t *r,const char *s,uint32_t n,uint16_t c) { (void)u;queued++;return hk_display_text(&power_display,r,s,n,c); }
static hk_result_t blit(void *u,const hk_display_rect_t *r,const hk_buffer_view_t *b,uint32_t f) { (void)u;queued++;return hk_display_blit(&power_display,r,b,f); }
static hk_result_t invalidate(void *u,const hk_display_rect_t *r) { hk_display_rect_t full={0,0,power_info.width,power_info.height};(void)u;return hk_display_mark_dirty(&power_display,r?r:&full); }
static hk_result_t lock(void *u,hk_display_surface_t *s) { (void)u; assert(!locked && !queued); assert(hk_display_abort(&power_display)==HK_OK); locked=1;return hk_display_surface_acquire(&power_display,s); }
static void save_panel(const char *name) {
    unsigned x,y; char path[120]; FILE *f;
    snprintf(path,sizeof(path),"build/power-real-%u-%s.ppm",power_info.width,name);f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n%u %u\n255\n",power_info.width,power_info.height);
    for(y=0;y<power_info.height;y++) for(x=0;x<power_info.width;x++) {
        unsigned offset=(y*power_info.width+x)*2;uint16_t c=(uint16_t)((s_panel[offset]<<8)|s_panel[offset+1]);
        fputc(((c>>11)&31)*255/31,f);fputc(((c>>5)&63)*255/63,f);fputc((c&31)*255/31,f);
    } fclose(f);
}
static void input_event(uint32_t pressed,uint32_t released) {
    hk_app_event_t e={.kind=HK_APP_EVENT_INPUT};e.data.input.pressed=pressed;e.data.input.released=released;
    assert(power_v2_entry.event(NULL,&e)==HK_OK);
}
static void render_frame(const char *name) {
    hk_app_surface_t surface;
    hk_app_surface_ops_t ops={.invalidate=invalidate,.clear=clear,.fill_rect=fill,.stroke_rect=stroke,.text=text,.blit=blit,.lock=lock};
    assert(hk_display_begin_batch(&power_display)==HK_OK);queued=0;locked=0;
    assert(hk_app_surface_private_init(&surface,1,&power_info,&ops)==HK_OK);
    assert(power_v2_entry.render(NULL,&surface)==HK_OK);
    assert(locked && queued==0 && surface.invalidated);
    assert(hk_display_present(&power_display,(hk_deadline_t){500000},NULL)==HK_OK);
    assert(s_begin_calls && s_end_calls==s_begin_calls);
    save_panel(name);
}
int main(void) {
    hk_display_rect_t dot={0,0,1,1}; unsigned i,x,y;
    fixture_reset();assert(hk_display_open(hk_display_service(),HK_DISPLAY_PLANE_BASE,&power_display)==HK_OK);
    assert(hk_display_get_info(&power_display,&power_info)==HK_OK);assert(power_info.maximum_commands==32);
    assert(hk_display_begin_batch(&power_display)==HK_OK);
    for(i=0;i<32;i++) assert(hk_display_fill_rect(&power_display,&dot,0x07e0)==HK_OK);
    assert(hk_display_fill_rect(&power_display,&dot,0x07e0)==HK_ERR_LIMIT);
    assert(hk_display_abort(&power_display)==HK_OK);
    assert(power_v2_entry.start(NULL)==HK_OK);
    for(i=0;i<3;i++) {
        char name[20];snprintf(name,sizeof(name),"select-%u",i);render_frame(name);
        /* The ordinary selector has no permanent hints below its centered block. */
        if(HK_BOARD_POWER_AXP173 || i!=1U)
            for(y=210;y<240;y++) for(x=0;x<power_info.width;x++) assert(fixture_panel_pixel(x,y)==0);
        /* Shared bitmap is exactly the foreground silhouette in each column. */
        for(y=0;y<48;y++) for(x=0;x<48;x++) {
            unsigned center=(2*i+1)*power_info.width/6;
            uint16_t expected=hk_power_icon_pixel(i,x,y) ? ((HK_BOARD_POWER_AXP173 || i!=1U)?0x07e0:0x4208) : 0;
            assert(fixture_panel_pixel(center-24+x,100+y)==expected);
        }
        input_event(HK_INPUT_BUTTON_RIGHT,0);
    }
    input_event(HK_INPUT_BUTTON_LEFT,0);input_event(HK_INPUT_BUTTON_OK,0);render_frame("waiting");
    action_result=HK_ERR_IO;
    {hk_app_event_t e={.kind=HK_APP_EVENT_TIMER};assert(power_v2_entry.event(NULL,&e)==HK_OK);}
    render_frame("error");
    assert(power_v2_entry.stop(NULL)==HK_OK);
    /* Invoke the actual menu callback through the real UI display binding. */
    assert(hk_ui_display_bind(&power_display)==HK_OK);
    {
        uint16_t tile_x=(uint16_t)((power_info.width-60U)/2U),tile_y=90U;
        hk_ui_display_fill_rect(0,0,(uint16_t)power_info.width,(uint16_t)power_info.height,0);
        hk_ui_display_draw_rect(tile_x,tile_y,60,60,2,0x07e0);
        power_draw_icon(tile_x,tile_y,0x07e0,0);save_panel("menu-icon");
        for(y=0;y<48;y++) for(x=0;x<48;x++)
            assert(fixture_panel_pixel(tile_x+6+x,tile_y+6+y)==(((hk_menu_icon_power[y*6U+x/8U] >> (7U-(x&7U))) & 1U)?0x07e0:0));
        hk_ui_display_fill_rect(tile_x,tile_y,60,60,0x07e0);
        hk_ui_display_draw_rect(tile_x+4,tile_y+4,52,52,2,0);
        power_draw_icon(tile_x,tile_y,0,0x07e0);save_panel("menu-selected");
        for(y=0;y<48;y++) for(x=0;x<48;x++)
            assert(fixture_panel_pixel(tile_x+6+x,tile_y+6+y)==(((hk_menu_icon_power[y*6U+x/8U] >> (7U-(x&7U))) & 1U)?0:0x07e0));
    }
    hk_ui_display_unbind();
    assert(hk_display_close(&power_display,(hk_deadline_t){500000})==HK_OK);
    puts("POWER_REAL_DISPLAY_OK queue-limit-reproduced centered selections single-press wait error thin-menu-icon");return 0;
}
