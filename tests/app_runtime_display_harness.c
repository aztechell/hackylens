#define main production_smoke_main
#include "app_runtime_production_harness.c"
#undef main

static uint8_t s_display_case;
static hk_result_t s_draw_result;
static hk_result_t s_lock_result;
static hk_result_t s_post_lock_draw_result;
static uint32_t s_abort_before_lock;
static uint32_t s_abort_after_lock;

static hk_result_t display_case_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}

static hk_result_t display_case_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    hk_display_rect_t rect = {0U, 0U, 1U, 1U};
    hk_buffer_view_t pixels = {0};
    hk_display_surface_t locked = {0};

    (void)ctx;
    s_draw_result = HK_OK;
    s_lock_result = HK_OK;
    s_post_lock_draw_result = HK_OK;
    if(s_display_case == 1U || s_display_case == 7U)
        s_draw_result = hk_app_surface_clear(surface, 0U);
    else if(s_display_case == 2U)
        s_draw_result = hk_app_surface_fill_rect(surface, &rect, 0U);
    else if(s_display_case == 3U)
        s_draw_result = hk_app_surface_stroke_rect(surface, &rect, 0U);
    else if(s_display_case == 4U)
        s_draw_result = hk_app_surface_text(surface, &rect, "x", 1U, 0U);
    else if(s_display_case == 5U)
        s_draw_result = hk_app_surface_blit(surface, &rect, &pixels, 0U);
    s_abort_before_lock = s_fixture.display_abort_count;
    s_lock_result = hk_app_surface_lock(surface, &locked);
    s_abort_after_lock = s_fixture.display_abort_count;
    if(s_display_case == 6U)
        s_post_lock_draw_result = hk_app_surface_clear(surface, 0U);
    return HK_OK;
}

static const hk_app_v2_entry_t s_display_entry = {
    .state_storage = s_secondary_state,
    .state_capacity_bytes = sizeof(s_secondary_state),
    .start = secondary_start,
    .event = display_case_event,
    .render = display_case_render,
    .stop = secondary_stop,
};

static const hk_app_t s_display_app = {
    .struct_size = sizeof(hk_app_t),
    .struct_version = HK_APP_DESCRIPTOR_VERSION,
    .id = "production-display-case",
    .title = "Production display case",
    .entry = &s_display_entry,
    .limits = {1024U, 256U, 64U, HK_APP_STATE_ALIGNMENT, 100U, 100U, 100U},
};

int main(void)
{
    if(production_smoke_main() != 0)
        return 1;
    for(s_display_case = 1U; s_display_case <= 7U; s_display_case++)
    {
        uint32_t present_before = s_fixture.display_present_count;
        uint32_t abort_before = s_fixture.display_abort_count;

        s_fixture.fail_next_clear = (uint8_t)(s_display_case == 7U);
        CHECK(app_runtime_integration_open(&s_display_app, NULL) == HK_OK);
        CHECK(app_runtime_integration_poll(s_fixture.now_us) == HK_OK);
        CHECK(s_fixture.display_present_count == present_before + 1U);
        if(s_display_case <= 5U)
        {
            CHECK(s_draw_result == HK_OK);
            CHECK(s_lock_result == HK_ERR_INVALID_STATE);
            CHECK(s_abort_before_lock == s_abort_after_lock);
            CHECK(s_fixture.display_abort_count == abort_before);
        }
        else
        {
            CHECK(s_lock_result == HK_OK);
            CHECK(s_fixture.display_abort_count == abort_before + 1U);
            if(s_display_case == 6U)
                CHECK(s_post_lock_draw_result == HK_ERR_INVALID_STATE);
            else
                CHECK(s_draw_result == HK_ERR_LIMIT);
        }
        CHECK(!s_fixture.batch_active && !s_fixture.surface_active);
        CHECK(app_runtime_integration_close(HK_APP_STOP_BACK) == HK_OK);
    }
    puts("APP_RUNTIME_DISPLAY_OK");
    return 0;
}
