#include "camera_vision_client.h"

#include <stddef.h>
#include <hackylens/capability/time.h>

#include "hk_config.h"
#include "camera_client.h"
#include "vision_result_service.h"
#if HK_ENABLE_APP_FACE_DETECT || HK_ENABLE_APP_FACE_RECOGNITION
#include "face_detect_detector.h"
#endif
#if HK_ENABLE_APP_OBJECT_DETECT
#include "object_detect_detector.h"
#endif

#define CAMERA_VISION_INFERENCE_TIMEOUT_US 2000000ULL

typedef struct
{
    ai_model_result_t (*load_step)(const char *package);
    hk_result_t (*submit)(const camera_preview_frame_t *frame);
    hk_result_t (*poll)(void);
    void (*close)(void);
    void (*collect)(vision_result_snapshot_t *snapshot);
} camera_detector_t;

typedef enum
{
    CAMERA_VISION_LOADING,
    CAMERA_VISION_READY,
    CAMERA_VISION_RUNNING,
} camera_vision_state_t;

static struct
{
    const void *owner;
    const camera_detector_t *detector;
    vision_source_t source;
    camera_vision_state_t state;
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint8_t rotate;
    hk_deadline_t deadline;
} s_vision;

#if HK_ENABLE_APP_FACE_DETECT || HK_ENABLE_APP_FACE_RECOGNITION || HK_ENABLE_APP_OBJECT_DETECT
static void add_box(vision_result_snapshot_t *snapshot, uint16_t id,
                    uint16_t confidence, int16_t x, int16_t y, int16_t w, int16_t h)
{
    snapshot->items[snapshot->count++] = vision_result_block(
        id, 0U, confidence, x, y, x + w, y + h, s_vision.height, s_vision.rotate);
}
#endif

#if HK_ENABLE_APP_FACE_DETECT || HK_ENABLE_APP_FACE_RECOGNITION
static void collect_faces(vision_result_snapshot_t *snapshot)
{
    uint8_t count;
    const face_detect_box_t *boxes = face_detect_detector_boxes(&count);
    for(uint8_t i = 0U; i < count && i < VISION_RESULT_MAX_ITEMS; i++)
        add_box(snapshot, i, 1000U, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h);
}

static const camera_detector_t s_face = {
    face_detect_detector_load_preview_step, face_detect_detector_submit_preview,
    face_detect_detector_poll_preview, face_detect_detector_unload, collect_faces,
};
#endif

#if HK_ENABLE_APP_OBJECT_DETECT
static void collect_objects(vision_result_snapshot_t *snapshot)
{
    uint8_t count;
    const object_detect_result_t *boxes = object_detect_detector_results(&count);
    for(uint8_t i = 0U; i < count && i < VISION_RESULT_MAX_ITEMS; i++)
        add_box(snapshot, boxes[i].class_id, boxes[i].confidence,
                boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h);
}

static const camera_detector_t s_objects = {
    object_detect_detector_load_preview_step, object_detect_detector_submit_preview,
    object_detect_detector_poll_preview, object_detect_detector_unload, collect_objects,
};
#endif

static const camera_detector_t *find_detector(vision_source_t source)
{
#if HK_ENABLE_APP_FACE_DETECT || HK_ENABLE_APP_FACE_RECOGNITION
    if(source == VISION_SOURCE_FACE)
        return &s_face;
#endif
#if HK_ENABLE_APP_OBJECT_DETECT
    if(source == VISION_SOURCE_OBJECT_DETECT)
        return &s_objects;
#endif
    (void)source;
    return NULL;
}

hk_result_t camera_vision_open_step(const void *owner, vision_source_t source,
                                   const char *package)
{
    if(!camera_client_is_open(owner))
        return HK_ERR_INVALID_STATE;
    const camera_detector_t *detector = find_detector(source);
    if(!detector)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(s_vision.owner && (s_vision.owner != owner || s_vision.source != source ||
                         s_vision.state != CAMERA_VISION_LOADING))
        return HK_ERR_BUSY;
    ai_model_result_t result = detector->load_step(package);
    if(result == AI_MODEL_RESULT_PENDING || result == AI_MODEL_RESULT_OK)
    {
        s_vision.owner = owner;
        s_vision.detector = detector;
        s_vision.source = source;
        s_vision.state = result == AI_MODEL_RESULT_OK ? CAMERA_VISION_READY : CAMERA_VISION_LOADING;
        return result == AI_MODEL_RESULT_OK ? HK_OK : HK_PENDING;
    }
    /* Failed loads already release the detector's resources. A busy detector
     * may belong to a native app, so close only a session we acquired. */
    camera_vision_close(owner);
    if(result == AI_MODEL_RESULT_BUSY)
        return HK_ERR_BUSY;
    if(result == AI_MODEL_RESULT_DESCRIPTOR)
        return HK_ERR_INVALID_ARGUMENT;
    if(result == AI_MODEL_RESULT_MANIFEST || result == AI_MODEL_RESULT_FORMAT ||
       result == AI_MODEL_RESULT_OUTPUT)
        return HK_ERR_VERSION_INCOMPATIBLE;
    return HK_ERR_IO;
}

void camera_vision_close(const void *owner)
{
    if(!owner || s_vision.owner != owner)
        return;
    s_vision.detector->close();
    s_vision.owner = NULL;
    s_vision.detector = NULL;
}

hk_result_t camera_vision_submit(const void *owner, uint32_t lease_id)
{
    if(!owner || s_vision.owner != owner)
        return HK_ERR_INVALID_STATE;
    if(s_vision.state != CAMERA_VISION_READY)
        return HK_ERR_BUSY;
    camera_preview_frame_t frame;
    hk_result_t result = camera_client_get(owner, lease_id, &frame);
    if(result != HK_OK)
        return result;
    result = hk_time_deadline_after_us(hk_time_service(),
                                      CAMERA_VISION_INFERENCE_TIMEOUT_US, &s_vision.deadline);
    if(result != HK_OK)
        return result;
    result = s_vision.detector->submit(&frame);
    if(result == HK_OK)
    {
        s_vision.state = CAMERA_VISION_RUNNING;
        s_vision.sequence = frame.sequence;
        s_vision.width = frame.width;
        s_vision.height = frame.height;
        s_vision.rotate = frame.rotate;
    }
    return result;
}

hk_result_t camera_vision_poll(const void *owner, vision_result_snapshot_t *snapshot)
{
    if(!snapshot)
        return HK_ERR_INVALID_ARGUMENT;
    if(!owner || s_vision.owner != owner || s_vision.state != CAMERA_VISION_RUNNING)
        return HK_ERR_INVALID_STATE;
    hk_result_t result = s_vision.detector->poll();
    if(result == HK_PENDING)
    {
        uint64_t now;
        result = hk_time_now_us(hk_time_service(), &now);
        if(result == HK_OK && now < s_vision.deadline.at_us)
            return HK_PENDING;
        if(result == HK_OK)
            result = HK_ERR_DEADLINE_EXCEEDED;
    }
    if(result != HK_OK)
    {
        camera_vision_close(owner);
        return result;
    }
    *snapshot = (vision_result_snapshot_t){
        .frame_id = s_vision.sequence,
        .source = s_vision.source,
        .width = s_vision.rotate ? s_vision.height : s_vision.width,
        .height = s_vision.rotate ? s_vision.width : s_vision.height,
    };
    s_vision.detector->collect(snapshot);
    s_vision.state = CAMERA_VISION_READY;
    return HK_OK;
}
