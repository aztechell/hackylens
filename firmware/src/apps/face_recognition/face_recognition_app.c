#include "face_recognition_app.h"

#include <stdio.h>
#include <string.h>
#include "hackylens_font_1bpp.h"

#include "../../config/camera_config.h"
#include "../../config/camera_layout.h"
#include "../../config/display_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../core/hk_string.h"
#include "../../core/shared_buffer.h"
#include "../../services/camera_frame.h"
#include "../../services/camera_light.h"
#include "../../services/camera_session.h"
#include "../../services/debug_console_service.h"
#include "../../services/media_workspace.h"
#include "../../services/resource_cleanup.h"
#include "../../services/vision_result_service.h"
#include "../../ui/camera_status_view.h"
#include "../../ui/menu_icons.h"
#include "face_recognition_image.h"

#define MATCH_MINIMUM 0.61f
#define MATCH_MARGIN 0.05f
#define LEARN_WINDOW_US 5000000ULL
#define INFERENCE_LIMIT_US 1000000ULL
#define DETECTOR_RESERVE (400U * 1024U)
_Static_assert(DETECTOR_RESERVE + 159768U + 256U + 3U * 128U * 128U <= MEDIA_WORKSPACE_BYTES,
               "detector, landmark and scratch must fit shared media storage");

typedef enum { SCAN, LANDMARKS, EMBEDDING } recognition_stage_t;
typedef enum { START_DONE, START_DETECTOR, START_LANDMARK, START_EMBEDDING, START_CAMERA } start_stage_t;

typedef struct
{
    const hk_input_t *input;
    const uint8_t *frame;
    const char *message;
    face_detect_box_t model_boxes[FACE_DETECT_BOX_MAX];
    vision_result_item_t items[FACE_DETECT_BOX_MAX];
    vision_result_item_t shown[FACE_DETECT_BOX_MAX];
    uint64_t learn_until;
    uint64_t stage_until;
    uint64_t ok_pressed_at;
    uint64_t now;
    uint32_t matches;
    uint32_t cycles;
    uint8_t index, count, shown_count, selected, enroll, confirm;
    uint8_t active, camera_started;
    start_stage_t starting;
    recognition_stage_t stage;
} recognition_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t g_state_storage[1024];
_Static_assert(sizeof(recognition_t) <= sizeof(g_state_storage), "recognition app state");
static recognition_t *g_active;
static ai_model_runtime_t g_landmark;
static ai_model_runtime_t g_embedding;
static face_gallery_t g_gallery;
static uint8_t g_cleanup_pending;
static media_workspace_t *g_workspace;

static uint16_t id_bit(uint8_t id) { return (uint16_t)(1U << (id - 1U)); }

static uint8_t cleanup_companions(void)
{
    ai_model_runtime_tick(&g_embedding);
    if(g_embedding.state == AI_MODEL_STATE_FAULT)
        return 1U;
    if(g_embedding.state != AI_MODEL_STATE_UNLOADED)
        return 0U;
    if(g_landmark.state == AI_MODEL_STATE_READY || g_landmark.state == AI_MODEL_STATE_RUNNING)
        ai_model_runtime_request_unload(&g_landmark);
    ai_model_runtime_tick(&g_landmark);
    if(g_landmark.state == AI_MODEL_STATE_FAULT ||
       face_detect_detector_model()->state == AI_MODEL_STATE_FAULT)
        return 1U;
    if(g_landmark.state != AI_MODEL_STATE_UNLOADED ||
       face_detect_detector_model()->state != AI_MODEL_STATE_UNLOADED)
        return 0U;
    media_workspace_release(&g_landmark);
    g_workspace = NULL;
    g_cleanup_pending = 0U;
    return 1U;
}

static void stop_pipeline(recognition_t *app, uint64_t deadline)
{
    vision_result_clear(VISION_SOURCE_FACE_RECOGNITION);
    if(app->camera_started)
    {
        camera_stop();
        camera_service_clear_mode();
        app->camera_started = 0U;
    }
    app->active = 0U;
    app->starting = START_DONE;
    app->frame = NULL;
    ai_model_runtime_limit_unload(&g_embedding, deadline);
    ai_model_runtime_limit_unload(&g_landmark, deadline);
    ai_model_runtime_request_unload(&g_embedding);
    face_detect_detector_limit_unload(deadline);
    face_detect_detector_unload();
    g_cleanup_pending = 1U;
    if(!cleanup_companions())
        (void)resource_cleanup_schedule(RESOURCE_CLEANUP_FACE_RECOGNITION, cleanup_companions);
}

static void app_error(recognition_t *app, const char *message)
{
    stop_pipeline(app, app->now + INFERENCE_LIMIT_US);
    app->message = message;
    camera_status_view_draw("FACE RECOG", message);
    printf("[FACE RECOG] %s\r\n", message);
}

static void start_pipeline(recognition_t *app, const hk_input_snapshot_t *input)
{
    resource_cleanup_poll();
    if(g_cleanup_pending)
    {
        if(g_landmark.state == AI_MODEL_STATE_FAULT || g_embedding.state == AI_MODEL_STATE_FAULT ||
           face_detect_detector_model()->state == AI_MODEL_STATE_FAULT)
            app_error(app, "RESTART REQUIRED");
        return;
    }
    const ai_model_runtime_t *primary = face_detect_detector_model();
    if(app->starting == START_DETECTOR)
    {
        if(!g_workspace)
            g_workspace = media_workspace_claim(&g_landmark);
        if(!g_workspace)
            return;
        const ai_model_workspace_t detector_workspace = {g_workspace->bytes, DETECTOR_RESERVE};
        face_detect_load_result_t detect = face_detect_detector_load(&detector_workspace);
        if(detect == FACE_DETECT_LOAD_BUSY)
            return;
        if(detect != FACE_DETECT_LOAD_OK)
        {
            app_error(app, face_detect_detector_error_label(detect));
            return;
        }
        ai_model_runtime_init(&g_landmark);
        ai_model_runtime_init(&g_embedding);
        app->starting = START_LANDMARK;
        return;
    }
    if(app->starting == START_LANDMARK || app->starting == START_EMBEDDING)
    {
        const ai_model_workspace_t landmark_workspace = {
            g_workspace->bytes + DETECTOR_RESERVE, MEDIA_WORKSPACE_BYTES - DETECTOR_RESERVE,
        };
        ai_model_result_t result = app->starting == START_LANDMARK ?
            ai_model_runtime_load_related(&g_landmark, &face_landmark_model,
                                          primary, &landmark_workspace) :
            ai_model_runtime_load_related_step(&g_embedding, &face_embedding_model, primary, NULL);
        if(result == AI_MODEL_RESULT_PENDING)
            return;
        if(result != AI_MODEL_RESULT_OK)
            app_error(app, ai_model_runtime_result_label(result));
        else
            app->starting = app->starting == START_LANDMARK ? START_EMBEDDING : START_CAMERA;
        return;
    }
    face_gallery_result_t gallery = face_gallery_load(&g_gallery, FACE_EMBEDDING_MODEL_CRC);
    if(gallery == FACE_GALLERY_IO)
    {
        app_error(app, "GALLERY SD ERROR");
        return;
    }
    if(gallery == FACE_GALLERY_CORRUPT)
        app->message = "BAD SLOT";
    camera_runtime_enter(CAMERA_RUNTIME_FACE_DETECT, input);
    app->camera_started = 1U;
    face_detect_detector_attach_camera();
    app->starting = START_DONE;
    app->active = 1U;
}

static uint8_t recognize_feature(const int8_t *feature, float *score)
{
    uint8_t id = 0U;
    float best = -1.0f, second = -1.0f;
    for(uint8_t i = 1U; i <= FACE_GALLERY_CAPACITY; i++)
    {
        if(!(g_gallery.occupied & id_bit(i)))
            continue;
        float candidate = face_recognition_compare(feature, g_gallery.features[i - 1U]);
        if(candidate > best)
        {
            second = best;
            best = candidate;
            id = i;
        }
        else if(candidate > second)
            second = candidate;
    }
    *score = best > 0.0f ? best : 0.0f;
    return best >= MATCH_MINIMUM && best - second >= MATCH_MARGIN ? id : 0U;
}

static void process_feature(recognition_t *app, const float *embedding)
{
    int8_t feature[FACE_GALLERY_DIM];
    float score;
    if(!face_recognition_encode(embedding, feature))
        return;
    uint8_t id = recognize_feature(feature, &score);
    if(app->enroll && app->count == 1U)
    {
        face_gallery_result_t saved = FACE_GALLERY_OCCUPIED;
        if(!id)
            saved = face_gallery_store(&g_gallery, app->enroll, FACE_EMBEDDING_MODEL_CRC, feature);
        if(saved == FACE_GALLERY_OK)
        {
            id = app->enroll;
            score = 1.0f;
            app->message = "FACE SAVED";
        }
        else
            app->message = id ? "ALREADY ID" :
                (saved == FACE_GALLERY_FULL ? "SD FULL" : "SAVE ERR");
        app->enroll = 0U;
    }
    app->items[app->index].id = id;
    app->items[app->index].confidence = (uint16_t)(score * 1000.0f + 0.5f);
    app->matches += id != 0U;
}

static void publish_cycle(recognition_t *app)
{
    uint16_t width, height;
    camera_service_frame_info(&width, &height);
    memcpy(app->shown, app->items, app->count * sizeof(app->items[0]));
    app->shown_count = app->count;
    app->cycles++;
    vision_result_publish(VISION_SOURCE_FACE_RECOGNITION, width, height,
                          app->shown, app->shown_count);
}

static uint8_t next_landmarks(recognition_t *app)
{
    while(app->index < app->count)
    {
        const face_detect_box_t *box = &app->model_boxes[app->index];
        if(box->w >= 24 && box->h >= 24)
        {
            uint8_t *input = hk_shared_buffer_uncached(g_landmark.scratch);
            face_recognition_crop(app->frame, box, input);
            app->stage = LANDMARKS;
            app->stage_until = app->now + INFERENCE_LIMIT_US;
            if(!ai_model_runtime_run(&g_landmark, input))
                app_error(app, "LANDMARK RUN");
            return app->active;
        }
        app->index++;
    }
    app->stage = SCAN;
    app->frame = NULL;
    publish_cycle(app);
    return 0U;
}

static uint8_t process_detection(const uint8_t *frame, const face_detect_box_t *model_boxes,
                                 const face_detect_box_t *sensor_boxes, uint8_t count, void *context)
{
    recognition_t *app = context;
    app->count = count;
    app->index = 0U;
    app->frame = frame;
    for(uint8_t i = 0U; i < count; i++)
    {
        const face_detect_box_t *box = &sensor_boxes[i];
        app->model_boxes[i] = model_boxes[i];
        app->items[i] = (vision_result_item_t){
            .kind = VISION_ITEM_BLOCK, .x0 = box->x, .y0 = box->y,
            .x1 = box->x + box->w, .y1 = box->y + box->h,
        };
    }
    return next_landmarks(app);
}

static void advance_recognition(recognition_t *app)
{
    if(app->stage == SCAN)
    {
        face_detect_detector_process_frame(process_detection, app);
        if(app->active && !face_detect_detector_ready())
            app_error(app, "DETECT RUN");
        return;
    }
    ai_model_runtime_t *model = app->stage == LANDMARKS ? &g_landmark : &g_embedding;
    if(!ai_model_runtime_take_completion(model))
    {
        if(app->now >= app->stage_until)
            app_error(app, "KPU TIMEOUT");
        return;
    }
    const uint8_t *output;
    size_t bytes;
    if(ai_model_runtime_get_output(model, 0U, &output, &bytes) != 0 ||
       bytes != model->descriptor->outputs[0].bytes)
    {
        app_error(app, "KPU OUTPUT");
        return;
    }
    if(app->stage == LANDMARKS)
    {
        uint8_t *input = hk_shared_buffer_uncached(g_landmark.scratch);
        if(face_recognition_align(app->frame, &app->model_boxes[app->index],
                                  (const float *)output, input))
        {
            app->stage = EMBEDDING;
            app->stage_until = app->now + INFERENCE_LIMIT_US;
            if(!ai_model_runtime_run(&g_embedding, input))
                app_error(app, "FEATURE RUN");
            return;
        }
    }
    else
        process_feature(app, (const float *)output);
    app->index++;
    if(!next_landmarks(app) && app->active)
        face_detect_detector_resume();
}

static void draw_overlay(camera_view_present_t *present, uint16_t width,
                         uint16_t height, uint32_t sequence, void *context)
{
    const recognition_t *app = context;
    char status[32];
    camera_view_frame_t frame = {
        .width = width, .height = height, .rotate = CAMERA_PREVIEW_ROTATE,
        .fit = CAMERA_PREVIEW_FIT,
    };
    (void)sequence;
    for(uint8_t i = 0U; i < app->shown_count; i++)
    {
        const vision_result_item_t *item = &app->shown[i];
        camera_view_rect_t source = {item->x0, item->y0, item->x1 - item->x0, item->y1 - item->y0};
        camera_view_rect_t mapped;
        uint16_t color = item->id ? COLOR_TERM_GREEN : COLOR_WHITE;
        camera_view_compose_rects(present, &frame, &source, 1U, color);
        if(camera_view_map_rect(&frame, &source, &mapped))
        {
            char label[12];
            if(item->id)
                snprintf(label, sizeof(label), "ID %u", (unsigned)item->id);
            else
                strcpy(label, "UNKNOWN");
            uint16_t label_width = (uint16_t)(strlen(label) * HACKYLENS_FONT_W);
            uint16_t x = mapped.x;
            uint16_t y = mapped.y >= HACKYLENS_FONT_H ? mapped.y - HACKYLENS_FONT_H : 0U;
            if(x + label_width > present->width)
                x = present->width - label_width;
            if(y + 2U * HACKYLENS_FONT_H > present->height)
                y = present->height - 2U * HACKYLENS_FONT_H;
            camera_view_compose_text_at(present, x, y, label, color, COLOR_BLACK);
        }
    }
    const char *text = app->confirm ? "FORGET? OK" :
        (app->enroll ? "ONE FACE" : app->message);
    if(!text)
        text = g_gallery.invalid & id_bit(app->selected) ? "INVALID" :
            (g_gallery.occupied & id_bit(app->selected) ? "SAVED" : "EMPTY");
    snprintf(status, sizeof(status), "ID%02u %s", (unsigned)app->selected, text);
    camera_view_compose_text_at(present, 0U, present->height - HACKYLENS_FONT_H,
                                status, COLOR_WHITE, COLOR_BLACK);
}

static void request_enrollment(recognition_t *app)
{
    if((g_gallery.occupied | g_gallery.invalid) & id_bit(app->selected))
    {
        app->message = "SLOT USED";
        return;
    }
    app->enroll = app->selected;
    app->learn_until = app->now + LEARN_WINDOW_US;
    app->message = NULL;
}

static hk_result_t app_start(const hk_app_context_t *ctx)
{
    void *storage;
    uint32_t bytes;
    if(hk_app_context_state(ctx, &storage, &bytes) != HK_OK || bytes < sizeof(recognition_t))
        return HK_ERR_INTERNAL;
    recognition_t *app = storage;
    if(hk_app_context_input(ctx, &app->input) != HK_OK)
        return HK_ERR_INTERNAL;
    g_active = app;
    app->selected = 1U;
    app->starting = START_DETECTOR;
    camera_status_view_draw("FACE RECOG", "LOADING MODELS");
    return HK_OK;
}

static hk_result_t app_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    recognition_t *app = g_active;
    if(!app || !event)
        return HK_ERR_INVALID_ARGUMENT;
    app->now = event->timestamp_us;
    if(event->kind == HK_APP_EVENT_MEDIA &&
       (event->data.media.kind == HK_APP_MEDIA_REMOVED || event->data.media.kind == HK_APP_MEDIA_ERROR))
    {
        if(app->active || app->starting)
            app_error(app, "SD REMOVED");
        return HK_OK;
    }
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        hk_input_snapshot_t input = {0};
        if(hk_input_get_state(app->input, &input.state) != HK_OK)
            return HK_ERR_INTERNAL;
        if(app->starting)
            start_pipeline(app, &input);
        if(app->active)
        {
            if(app->enroll && app->now >= app->learn_until)
            {
                app->enroll = 0U;
                app->message = "NO SAMPLE";
            }
            advance_recognition(app);
            if(app->active)
                camera_runtime_tick_with_pipeline(&input, NULL, NULL, draw_overlay, app);
        }
        return HK_OK;
    }
    if(event->kind != HK_APP_EVENT_INPUT)
        return HK_OK;
    uint32_t changed = event->data.input.changed;
    uint32_t pressed = changed & event->data.input.state;
    if(pressed & HK_INPUT_BUTTON_BACK)
    {
        if(!app->confirm && !app->enroll)
            return hk_app_context_request_close(ctx);
        app->confirm = app->enroll = 0U;
        app->message = NULL;
    }
    if(!app->active)
        return HK_OK;
    if(pressed & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_RIGHT))
    {
        app->selected = pressed & HK_INPUT_BUTTON_LEFT ?
            (app->selected == 1U ? FACE_GALLERY_CAPACITY : app->selected - 1U) :
            (app->selected == FACE_GALLERY_CAPACITY ? 1U : app->selected + 1U);
        app->confirm = app->enroll = 0U;
        app->message = NULL;
    }
    if(pressed & HK_INPUT_BUTTON_OK)
        app->ok_pressed_at = app->now;
    else if((changed & HK_INPUT_BUTTON_OK) && app->ok_pressed_at)
    {
        uint8_t held = app->now - app->ok_pressed_at >= CAMERA_OK_HOLD_US;
        app->ok_pressed_at = 0U;
        if(app->confirm && !held)
        {
            app->message = face_gallery_forget(&g_gallery, app->selected) == FACE_GALLERY_OK ?
                "FORGOTTEN" : "DELETE ERR";
            app->confirm = 0U;
        }
        else if(held)
        {
            app->enroll = 0U;
            app->confirm = ((g_gallery.occupied | g_gallery.invalid) & id_bit(app->selected)) != 0U;
        }
        else
            request_enrollment(app);
    }
    return HK_OK;
}

static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline = {0};
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    if(g_active && (g_active->active || g_active->starting))
        stop_pipeline(g_active, deadline.at_us);
    g_active = NULL;
    memset(&g_gallery, 0, sizeof(g_gallery));
    hk_result_t light_result = camera_light_retire(deadline);
    return result == HK_OK ? light_result : result;
}

uint8_t face_recognition_handle_debug_command(const char *cmd)
{
    if(str_eq_ci(cmd, "HKFRLEARN"))
    {
        if(g_active && g_active->active)
            request_enrollment(g_active);
        debug_console_write_text(g_active && g_active->enroll ? "HKFRLEARN OK\r\n" : "HKFRLEARN ERROR\r\n");
        return 1U;
    }
    if(!str_eq_ci(cmd, "HKFRINFO"))
        return 0U;
    if(!g_active)
        debug_console_write_text("HKFRINFO active=0\r\n");
    else
        printf("HKFRINFO active=%u stage=%u slots=%03x invalid=%03x cycles=%u faces=%u id=%u score=%u known=%u ld=%u/%u fe=%u/%u status=%s\r\n",
               g_active->active, g_active->stage, g_gallery.occupied, g_gallery.invalid,
               (unsigned)g_active->cycles, g_active->shown_count,
               g_active->shown_count ? g_active->shown[0].id : 0U,
               g_active->shown_count ? g_active->shown[0].confidence : 0U,
               (unsigned)g_active->matches,
               (unsigned)g_landmark.run_count, (unsigned)g_landmark.last_inference_us,
               (unsigned)g_embedding.run_count, (unsigned)g_embedding.last_inference_us,
               g_active->message ? g_active->message : "NONE");
    return 1U;
}

void face_recognition_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_face_detect);
}

const hk_app_v2_entry_t face_recognition_v2_entry = {
    .state_storage = g_state_storage, .state_capacity_bytes = sizeof(g_state_storage),
    .start = app_start, .event = app_event, .stop = app_stop,
};
