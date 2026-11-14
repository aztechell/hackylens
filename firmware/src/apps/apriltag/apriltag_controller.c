#include "apriltag_controller.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <hackylens/capability/time.h>

#include "../../config/camera_config.h"
#include "../../config/display_config.h"
#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../services/camera_frame.h"
#include "../../services/camera_session.h"
#include "../../services/camera_session_preferences.h"
#include "../../services/core1_executor.h"
#include "../../services/vision_result_service.h"
#include "../../ui/camera_status_view.h"
#include "../../ui/camera_view.h"
#include "apriltag_detector.h"
#include "apriltag_distance.h"
#include "apriltag_config.h"
#include "apriltag_settings.h"
#include "apriltag_settings_menu.h"
#include "apriltag_view.h"

static uint8_t g_error;
static uint64_t g_start_deadline_us;
static apriltag_result_t g_display_results[APRILTAG_RESULT_MAX];
static uint8_t g_display_count;
static uint8_t g_display_misses;
static uint32_t g_display_sequence;
static uint32_t g_preview_count;
static uint32_t g_read_count;
static int16_t g_far_x = -1;
static int16_t g_far_y = -1;
static uint8_t g_far_misses;
static uint8_t g_far_detail;
static uint8_t g_far_small_tag;
static uint64_t g_far_detail_due_us;
static int16_t g_target_id = -1;
static settings_menu_session_t g_settings_menu;
static const hk_accelerometer_t *g_sensor;
static uint8_t g_calibration_armed;
static uint8_t g_calibration_count;
static float g_calibration_sum;
static int16_t g_calibration_id;
static uint32_t g_range_mm;
static uint32_t g_horizontal_mm;
static int16_t g_measured_id = -1;
static uint8_t g_level_ready;
static uint8_t g_level_steady;
static uint64_t g_gravity_due_us;
static int16_t g_gravity[3];

static void clear_measurement(void)
{
    g_range_mm = 0U;
    g_horizontal_mm = 0U;
    g_measured_id = -1;
    g_level_ready = 0U;
    g_calibration_count = 0U;
}

static uint8_t sample_gravity(int16_t sample[3])
{
    hk_acceleration_t raw;
    if(!g_sensor || hk_accelerometer_read(g_sensor, &raw) != HK_OK)
        return 0U;
    int32_t x = raw.x_mg, y = raw.y_mg, z = raw.z_mg;
    if(x < -1150 || x > 1150 || y < -1150 || y > 1150 || z < -1150 || z > 1150)
        return 0U;
    int32_t magnitude_sq = x * x + y * y + z * z;
    if(magnitude_sq < 850L * 850L || magnitude_sq > 1150L * 1150L)
        return 0U;
    sample[0] = raw.x_mg;
    sample[1] = raw.y_mg;
    sample[2] = raw.z_mg;
    return 1U;
}

static void track_gravity(void)
{
    int16_t sample[3];
    if(!sample_gravity(sample))
    {
        g_level_steady = 0U;
        return;
    }
    int32_t change = 0;
    for(unsigned i = 0; i < 3U; i++)
    {
        int32_t delta = sample[i] - g_gravity[i];
        change += delta * delta;
    }
    memcpy(g_gravity, sample, sizeof(g_gravity));
    if(change > 45L * 45L)
        g_level_steady = 1U;
    else if(g_level_steady < 5U)
        g_level_steady++;
}

static void measure(const apriltag_result_t *tag)
{
    apriltag_distance_settings_t *settings = apriltag_settings_distance();
    if(!tag || !apriltag_settings_selected(tag->id))
    {
        clear_measurement();
        return;
    }
    if(g_calibration_armed)
    {
        float focal;
        if(tag->hamming || !apriltag_distance_calibrate(
               tag->corners, settings->tag_mm, settings->reference_mm, &focal))
        {
            g_calibration_count = 0U;
            return;
        }
        if(!g_calibration_count || g_calibration_id != tag->id ||
           fabsf(focal - g_calibration_sum / g_calibration_count) > focal * 0.05f)
        {
            g_calibration_sum = 0.0f;
            g_calibration_count = 0U;
            g_calibration_id = (int16_t)tag->id;
        }
        g_calibration_sum += focal;
        if(++g_calibration_count < 5U)
            return;
        float mean_focal_px = g_calibration_sum / g_calibration_count;
        settings->focal_millipixels = (uint32_t)(mean_focal_px * 100.0f + 0.5f) * 10U;
        apriltag_settings_save_distance();
        g_calibration_armed = 0U;
        g_calibration_count = 0U;
    }

    apriltag_vector_t translation;
    if(!apriltag_distance_pose(tag->corners, settings->tag_mm,
            settings->focal_millipixels / 1000.0f, tag->native_resolution, &translation))
    {
        clear_measurement();
        return;
    }

    g_measured_id = (int16_t)tag->id;
    g_range_mm = (uint32_t)(apriltag_distance_norm(translation) + 0.5f);
    g_horizontal_mm = 0U;
    g_level_ready = 0U;
    if(settings->level && g_level_steady == 5U)
    {
        float horizontal;
        /* The gravity references are taken with the displayed image upright. */
        if(CAMERA_PREVIEW_ROTATE)
            translation = (apriltag_vector_t){-translation.y, translation.x, translation.z};
        if(apriltag_distance_horizontal(translation, settings->upright,
                                       settings->lens_down, g_gravity, &horizontal))
        {
            g_horizontal_mm = (uint32_t)(horizontal + 0.5f);
            g_level_ready = 1U;
        }
    }
}

static void draw_measurement(camera_view_present_t *present)
{
    const apriltag_distance_settings_t *settings = apriltag_settings_distance();
    const char *label;
    char text[32];
    if(g_calibration_armed)
    {
        if(apriltag_settings_selected_count() == 0U)
            label = "SELECT A TAG";
        else
        {
            snprintf(text, sizeof(text), "CAL %u mm %u/5",
                     (unsigned)settings->reference_mm, (unsigned)g_calibration_count);
            label = text;
        }
    }
    else if(!settings->focal_millipixels)
        label = "CALIBRATE FIRST";
    else if(g_range_mm)
    {
        snprintf(text, sizeof(text), "#%u %u.%u cm",
                 (unsigned)g_measured_id, (unsigned)(g_range_mm / 10U),
                 (unsigned)(g_range_mm % 10U));
        label = text;
    }
    else
        label = "SELECTED TAG --";
    uint16_t y = present->height - HACKYLENS_FONT_H;
    camera_view_compose_text_at(present, 4U, y, label, hk_ui_accent(), COLOR_BLACK);
    if(settings->level && g_sensor && g_range_mm)
    {
        if(g_level_ready && g_level_steady == 5U)
            snprintf(text, sizeof(text), "H %u.%u cm", (unsigned)(g_horizontal_mm / 10U),
                     (unsigned)(g_horizontal_mm % 10U));
        else
            snprintf(text, sizeof(text), "H --");
        camera_view_compose_text_at(present, 4U, y - HACKYLENS_FONT_H,
                                    text, hk_ui_accent(), COLOR_BLACK);
    }
}

static void apriltag_display_reset(void)
{
    g_display_count = 0U;
    g_display_misses = 0U;
    g_display_sequence = 0U;
    g_target_id = -1;
    g_far_x = -1;
    g_far_y = -1;
    g_far_misses = 0U;
    g_far_detail = 0U;
    g_far_small_tag = 0U;
    g_far_detail_due_us = 0U;
    clear_measurement();
}

static uint8_t tag_contains_center(const apriltag_result_t *tag)
{
    const float x = APRILTAG_INPUT_W * 0.5f;
    const float y = APRILTAG_INPUT_H * 0.5f;
    uint8_t positive = 0U, negative = 0U;
    for(uint8_t i = 0; i < 4U; i++)
    {
        const float *a = tag->corners[i], *b = tag->corners[(i + 1U) % 4U];
        float side = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
        positive |= side > 0.0f;
        negative |= side < 0.0f;
    }
    return positive != negative;
}

static const apriltag_result_t *apriltag_update_target(uint16_t width, uint16_t height)
{
    int32_t center_x = width / 2U;
    int32_t center_y = height / 2U;
    uint32_t best_distance = 0xFFFFFFFFUL;
    uint32_t best_selected_distance = UINT32_MAX;
    const apriltag_result_t *selected = NULL;

    g_target_id = -1;
    for(uint8_t i = 0U; i < g_display_count; i++)
    {
        const apriltag_result_t *result = &g_display_results[i];
        int32_t dx;
        int32_t dy;
        uint32_t distance;

        dx = result->center_x - center_x;
        dy = result->center_y - center_y;
        distance = (uint32_t)(dx * dx + dy * dy);
        uint8_t centered = tag_contains_center(result);
        if(centered && distance < best_distance)
        {
            best_distance = distance;
            g_target_id = (int16_t)result->id;
        }
        if(distance < best_selected_distance && (!g_calibration_armed || centered) &&
           apriltag_settings_selected(result->id))
        {
            best_selected_distance = distance;
            selected = result;
        }
    }
    return selected;
}

static void apriltag_update_results(void)
{
    uint32_t sequence;
    uint8_t count;
    const apriltag_result_t *results = apriltag_detector_results(&count, &sequence);
    const apriltag_preferences_t *preferences = apriltag_settings_preferences();

    if(sequence == 0U || sequence == g_display_sequence)
        return;
    g_display_sequence = sequence;
    uint16_t width, height;
    camera_service_frame_info(&width, &height);
    vision_result_item_t items[APRILTAG_RESULT_MAX];
    uint8_t published_count = 0U;
    const apriltag_result_t *nearest = NULL;
    uint32_t best = UINT32_MAX;
    for(uint8_t i = 0; i < count; i++)
    {
        const apriltag_result_t *tag = &results[i];
        uint8_t selected = apriltag_settings_selected(tag->id);
        if(preferences->far_search)
        {
            int32_t dx = tag->center_x - width / 2, dy = tag->center_y - height / 2;
            /* Prefer selected targets, then the one nearest the crosshair. */
            uint32_t score = (uint32_t)(dx * dx + dy * dy) +
                             (selected ? 0U : (uint32_t)width * height);
            if(score < best)
            {
                nearest = tag;
                best = score;
            }
        }
        if(preferences->output_mode == APRILTAG_OUTPUT_SELECTED && !selected)
            continue;
        items[published_count++] = vision_result_block(
            tag->id, tag->hamming, tag->confidence, tag->x, tag->y,
            tag->x + tag->w, tag->y + tag->h, height, 0U);
    }
    if(nearest)
    {
        g_far_x = nearest->center_x;
        g_far_y = nearest->center_y;
        g_far_misses = 0U;
        g_far_small_tag = nearest->w <= APRILTAG_FAR_TRACK_MAX_TAG &&
                          nearest->h <= APRILTAG_FAR_TRACK_MAX_TAG;
    }
    else if(preferences->far_search)
    {
        g_far_detail = 0U;
        g_far_small_tag = 0U;
        if(++g_far_misses == APRILTAG_FAR_FULL_FRAME_PERIOD)
        {
            g_far_x = -1;
            g_far_y = -1;
            g_far_misses = 0U;
        }
    }
    vision_result_publish(VISION_SOURCE_APRILTAG, width, height, items, published_count);
    if(count)
    {
        g_read_count++;
        memcpy(g_display_results, results, (size_t)count * sizeof(g_display_results[0]));
        g_display_count = count;
        g_display_misses = 0U;
        const apriltag_result_t *selected = apriltag_update_target(width, height);
        if(apriltag_settings_distance()->enabled)
            measure(selected);
        return;
    }
    clear_measurement();
    g_target_id = -1;
    if(g_display_count && g_display_misses < APRILTAG_DISPLAY_HOLD_MISSES)
    {
        g_display_misses++;
        return;
    }
    g_display_count = 0U;
    g_display_misses = 0U;
}

static uint16_t window_origin(int16_t center, uint16_t frame, uint16_t window)
{
    if(center < 0)
        return (frame - window) / 2U;
    int32_t origin = center - window / 2;
    if(origin < 0)
        return 0U;
    if(origin > frame - window)
        return frame - window;
    return (uint16_t)origin;
}

static void apriltag_consume_frame(const volatile uint16_t *pixels,
                                   uint16_t width,
                                   uint16_t height,
                                   uint32_t sequence,
                                   void *context)
{
    (void)context;
    (void)sequence;
    if(!apriltag_settings_preferences()->far_search ||
       width != APRILTAG_INPUT_W * 2U || height != APRILTAG_INPUT_H * 2U ||
       g_far_misses == APRILTAG_FAR_FULL_FRAME_PERIOD - 1U)
    {
        (void)apriltag_detector_submit(pixels, width, height, NULL);
        return;
    }
    uint64_t now = 0U;
    (void)hk_time_now_us(hk_time_service(), &now);
    uint16_t window_width = APRILTAG_INPUT_W, window_height = APRILTAG_INPUT_H;
    if(g_far_detail && g_far_small_tag)
        window_width = window_height = APRILTAG_FAR_TRACK_SIZE;
    const apriltag_scan_region_t region = {
        .x = window_origin(g_far_x, width, window_width),
        .y = window_origin(g_far_y, height, window_height),
        .width = window_width,
        .height = window_height,
        .full_detail = g_far_detail || (g_far_misses && now >= g_far_detail_due_us),
    };
    if(apriltag_detector_submit(pixels, width, height, &region))
    {
        g_far_detail = region.full_detail;
        if(region.full_detail)
            g_far_detail_due_us = now + APRILTAG_FAR_DETAIL_INTERVAL_US;
    }
}

static void start_camera(const hk_input_snapshot_t *input)
{
    g_start_deadline_us = 0U;
    g_error = !apriltag_detector_init();
    if(g_error)
    {
        vision_result_clear(VISION_SOURCE_APRILTAG);
        camera_status_view_draw("TAG ERROR", "DETECTOR INIT");
        return;
    }
    camera_runtime_enter(CAMERA_RUNTIME_APRILTAG, input);
}

static void apriltag_compose_overlay(camera_view_present_t *present,
                                     uint16_t width,
                                     uint16_t height,
                                     uint32_t sequence,
                                     void *context)
{
    uint8_t selected[APRILTAG_RESULT_MAX] = {0};

    (void)context;
    (void)sequence;
    uint8_t distance_enabled = apriltag_settings_distance()->enabled;
    for(uint8_t i = 0U; i < g_display_count; i++)
        selected[i] = apriltag_settings_selected(g_display_results[i].id);
    apriltag_view_compose_results(present, width, height,
                                  g_display_results, selected, g_display_count);
    if(distance_enabled)
        draw_measurement(present);
    if(apriltag_settings_preferences()->far_search)
        camera_view_compose_text_at(present, present->width - 4U - 3U * HACKYLENS_FONT_W,
                                    present->height - HACKYLENS_FONT_H,
                                    "FAR", hk_ui_accent(), COLOR_BLACK);
}

void apriltag_controller_enter(const hk_input_snapshot_t *input, const hk_accelerometer_t *sensor)
{
    g_sensor = sensor;
    g_level_steady = 0U;
    g_gravity_due_us = 0U;
    g_preview_count = 0U;
    g_read_count = 0U;
    g_calibration_armed = 0U;
    apriltag_display_reset();
    settings_menu_close(&g_settings_menu);
    apriltag_settings_load();
    apriltag_settings_apply_session();
    g_error = 0U;
    hk_deadline_t deadline;
    /* Let the system tick finish the previous app's resource cleanup before
     * claiming the camera, even if its core-1 job just became idle. */
    if(hk_time_deadline_after_us(hk_time_service(), APRILTAG_START_TIMEOUT_US,
                                &deadline) == HK_OK)
    {
        g_start_deadline_us = deadline.at_us;
        return;
    }
    start_camera(input);
}

void apriltag_controller_exit(void)
{
    settings_menu_close(&g_settings_menu);
    apriltag_display_reset();
    g_sensor = NULL;
    g_calibration_armed = 0U;
    g_start_deadline_us = 0U;
    vision_result_clear(VISION_SOURCE_APRILTAG);
    camera_stop();
    apriltag_detector_deinit();
    camera_session_preferences_clear();
}

void apriltag_controller_tick(const hk_input_snapshot_t *input)
{
    if(g_start_deadline_us)
    {
        uint64_t now = 0U;
        if(core1_executor_init() && core1_executor_idle())
            start_camera(input);
        else if(hk_time_now_us(hk_time_service(), &now) != HK_OK ||
                now >= g_start_deadline_us)
        {
            g_start_deadline_us = 0U;
            g_error = 1U;
            camera_status_view_draw("TAG ERROR", "DETECTOR BUSY");
        }
        return;
    }
    if(settings_menu_active(&g_settings_menu))
    {
        settings_menu_tick(&g_settings_menu, input);
        return;
    }
    if(camera_runtime_ok_hold_triggered(input))
    {
        g_calibration_armed = 0U;
        apriltag_display_reset();
        apriltag_detector_discard_results();
        vision_result_clear(VISION_SOURCE_APRILTAG);
        camera_service_freeze(1U);
        (void)settings_menu_open(&g_settings_menu, apriltag_settings_menu_definition(&g_settings_menu));
        return;
    }

    if(g_error)
        return;
    const apriltag_distance_settings_t *distance = apriltag_settings_distance();
    uint64_t now;
    if(g_sensor && distance->enabled && distance->level &&
       hk_time_now_us(hk_time_service(), &now) == HK_OK && now >= g_gravity_due_us)
    {
        track_gravity();
        g_gravity_due_us = now + APRILTAG_GRAVITY_INTERVAL_US;
    }
    apriltag_update_results();
    if(camera_runtime_tick_paced(input, apriltag_consume_frame, NULL,
                                 apriltag_compose_overlay, NULL, APRILTAG_PREVIEW_INTERVAL_US))
        g_preview_count++;
}

uint8_t apriltag_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    camera_runtime_input_event_t event;

    if(settings_menu_active(&g_settings_menu))
    {
        if(settings_menu_handle_input(&g_settings_menu, input) == SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        {
            settings_menu_close(&g_settings_menu);
            g_level_steady = 0U;
            if(!apriltag_settings_distance()->enabled)
                g_calibration_armed = 0U;
            camera_service_resume_from_settings(input && (input->state & BUTTON_OK));
            camera_view_clear();
        }
        return 0U;
    }
    if(g_error || g_start_deadline_us)
    {
        if(input && (input->pressed & BUTTON_BACK))
            return 1U;
        return 0U;
    }
    event = camera_runtime_handle_input(input);
    if(event == CAMERA_RUNTIME_INPUT_EXIT)
        return 1U;
    if(event == CAMERA_RUNTIME_INPUT_OK_RELEASE)
    {
        if(g_calibration_armed)
            apriltag_controller_cancel_calibration();
        else if(g_target_id >= 0 && !g_display_misses)
        {
            (void)apriltag_settings_toggle_selected((uint16_t)g_target_id);
            clear_measurement();
        }
    }
    return 0U;
}

int16_t apriltag_controller_target_id(void)
{
    return g_target_id;
}

uint8_t apriltag_controller_has_accelerometer(void)
{
    return g_sensor != NULL;
}

uint8_t apriltag_controller_capture_gravity(uint8_t lens_down)
{
    apriltag_distance_settings_t *settings = apriltag_settings_distance();
    int16_t *reference = lens_down ? settings->lens_down : settings->upright;
    if(!sample_gravity(reference))
        return 0U;
    apriltag_settings_save_distance();
    return 1U;
}

uint8_t apriltag_controller_arm_calibration(void)
{
    if(!apriltag_settings_distance()->enabled)
        return 0U;
    g_calibration_count = 0U;
    g_calibration_armed = 1U;
    return 1U;
}

uint8_t apriltag_controller_calibration_armed(void)
{
    return g_calibration_armed;
}

void apriltag_controller_cancel_calibration(void)
{
    g_calibration_armed = 0U;
    g_calibration_count = 0U;
}

uint32_t apriltag_controller_range_mm(void)
{
    return g_range_mm;
}

uint32_t apriltag_controller_horizontal_mm(void)
{
    return g_level_ready && g_level_steady == 5U ? g_horizontal_mm : 0U;
}

void apriltag_controller_stats(uint32_t *preview_count, uint32_t *read_count)
{
    *preview_count = g_preview_count;
    *read_count = g_read_count;
}
