#include "apriltag_settings.h"

#include <string.h>

#include "apriltag_config.h"
#include "apriltag_detector.h"
#include "apriltag_distance.h"
#include "../../core/hk_binary.h"
#include "../../core/camera_types.h"
#include "../../services/camera_light.h"
#include "../../services/camera_session_preferences.h"
#include "../../services/settings_app_data.h"
#include "../../services/settings_persistence.h"

#define APRILTAG_DATA_SCHEMA 0U
#define APRILTAG_DATA_FLAGS 1U
#define APRILTAG_DATA_LIGHT 2U
#define APRILTAG_DATA_RED 3U
#define APRILTAG_DATA_GREEN 4U
#define APRILTAG_DATA_BLUE 5U
#define APRILTAG_DATA_SELECTED 6U
#define APRILTAG_DATA_BYTES (APRILTAG_DATA_SELECTED + APRILTAG_SELECTED_BYTES)

#define APRILTAG_FLAG_REFINE 0x01U
#define APRILTAG_FLAG_SELECTED_OUTPUT 0x02U
#define APRILTAG_FLAG_FPS 0x04U
#define APRILTAG_FLAG_FAR_SEARCH 0x08U
#define APRILTAG_SEN0305_DEFAULT_FOCAL_MPX 154000U
#define APRILTAG_CUBE_DEFAULT_FOCAL_MPX 145000U

_Static_assert(APRILTAG_DATA_BYTES <= SETTINGS_APP_DATA_V2_SIZE,
               "APRILTAG settings overlap another app-data partition");

static apriltag_preferences_t g_preferences;
static uint8_t g_selected[APRILTAG_SELECTED_BYTES];
static uint8_t g_loaded;
static apriltag_distance_settings_t g_distance;
static uint8_t g_distance_loaded;

apriltag_distance_settings_t *apriltag_settings_distance(void)
{
    if(!g_distance_loaded)
    {
        uint8_t data[SETTINGS_TAG_DISTANCE_BYTES];
        settings_tag_distance_read(data);
        g_distance = (apriltag_distance_settings_t){
            .tag_mm = APRILTAG_DISTANCE_TAG_DEFAULT_MM,
            .reference_mm = APRILTAG_DISTANCE_REFERENCE_DEFAULT_MM,
            .focal_millipixels = strcmp(HK_BOARD_ID, "huskylens-sen0305") == 0 ?
                                  APRILTAG_SEN0305_DEFAULT_FOCAL_MPX : 0U,
        };
        if(strcmp(HK_BOARD_ID, "sipeed-maix-cube") == 0)
            g_distance.focal_millipixels = APRILTAG_CUBE_DEFAULT_FOCAL_MPX;
        if(data[0] == 1U)
        {
            g_distance.enabled = (data[1] & 1U) != 0U;
            g_distance.level = (data[1] & 2U) != 0U;
            uint16_t side = rd16(data + 2U), reference = rd16(data + 4U);
            uint32_t focal = rd32(data + 6U);
            if(side >= 20U && side <= 150U)
                g_distance.tag_mm = side;
            if(reference >= 100U && reference <= 1000U)
                g_distance.reference_mm = reference;
            if(focal >= APRILTAG_DISTANCE_FOCAL_MIN_MPX &&
               focal <= APRILTAG_DISTANCE_FOCAL_MAX_MPX)
                g_distance.focal_millipixels = (focal + 5U) / 10U * 10U;
            for(unsigned i = 0; i < 3U; i++)
            {
                g_distance.upright[i] = (int16_t)rd16(data + 10U + i * 2U);
                g_distance.lens_down[i] = (int16_t)rd16(data + 16U + i * 2U);
            }
        }
        g_distance_loaded = 1U;
    }
    return &g_distance;
}

void apriltag_settings_save_distance(void)
{
    uint8_t data[SETTINGS_TAG_DISTANCE_BYTES] = {1U};
    const apriltag_distance_settings_t *distance = apriltag_settings_distance();
    data[1] = distance->enabled | (distance->level << 1U);
    wr16(data + 2U, distance->tag_mm);
    wr16(data + 4U, distance->reference_mm);
    wr32(data + 6U, distance->focal_millipixels);
    for(unsigned i = 0; i < 3U; i++)
    {
        wr16(data + 10U + i * 2U, (uint16_t)distance->upright[i]);
        wr16(data + 16U + i * 2U, (uint16_t)distance->lens_down[i]);
    }
    settings_tag_distance_write(data);
    settings_mark_dirty(1U);
}

static uint8_t clamp_percent(uint8_t value)
{
    return value <= 100U ? value : 100U;
}

static void apriltag_settings_defaults(void)
{
    memset(&g_preferences, 0, sizeof(g_preferences));
    memset(g_selected, 0, sizeof(g_selected));
    g_preferences.refine_edges = 0U;
    g_preferences.output_mode = APRILTAG_OUTPUT_ALL;
    g_preferences.fps_enabled = 1U;
    g_preferences.light_mode = CAMERA_LIGHT_LED;
    g_preferences.rgb_red = 100U;
    g_preferences.rgb_green = 100U;
    g_preferences.rgb_blue = 100U;
}

static void apriltag_settings_save(void)
{
    uint8_t data[SETTINGS_APP_DATA_SIZE];

    settings_app_data_read(data);
    memset(data, 0, APRILTAG_DATA_BYTES);
    data[APRILTAG_DATA_SCHEMA] = APRILTAG_SETTINGS_SCHEMA;
    data[APRILTAG_DATA_FLAGS] =
        (g_preferences.refine_edges ? APRILTAG_FLAG_REFINE : 0U) |
        (g_preferences.output_mode == APRILTAG_OUTPUT_SELECTED ? APRILTAG_FLAG_SELECTED_OUTPUT : 0U) |
        (g_preferences.fps_enabled ? APRILTAG_FLAG_FPS : 0U) |
        (g_preferences.far_search ? APRILTAG_FLAG_FAR_SEARCH : 0U);
    data[APRILTAG_DATA_LIGHT] = g_preferences.light_mode;
    data[APRILTAG_DATA_RED] = g_preferences.rgb_red;
    data[APRILTAG_DATA_GREEN] = g_preferences.rgb_green;
    data[APRILTAG_DATA_BLUE] = g_preferences.rgb_blue;
    memcpy(data + APRILTAG_DATA_SELECTED, g_selected, sizeof(g_selected));
    settings_app_data_write(data);
    settings_mark_dirty(0U);
}

void apriltag_settings_load(void)
{
    uint8_t data[SETTINGS_APP_DATA_SIZE];
    uint8_t flags;

    if(g_loaded)
        return;
    g_loaded = 1U;
    apriltag_settings_defaults();
    settings_app_data_read(data);
    if(data[APRILTAG_DATA_SCHEMA] != APRILTAG_SETTINGS_SCHEMA)
        return;

    flags = data[APRILTAG_DATA_FLAGS];
    g_preferences.refine_edges = (flags & APRILTAG_FLAG_REFINE) ? 1U : 0U;
    g_preferences.output_mode = (flags & APRILTAG_FLAG_SELECTED_OUTPUT) ?
                                APRILTAG_OUTPUT_SELECTED : APRILTAG_OUTPUT_ALL;
    g_preferences.fps_enabled = (flags & APRILTAG_FLAG_FPS) ? 1U : 0U;
    g_preferences.far_search = (flags & APRILTAG_FLAG_FAR_SEARCH) ? 1U : 0U;
    g_preferences.light_mode = data[APRILTAG_DATA_LIGHT] == CAMERA_LIGHT_RGB ?
                               CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    g_preferences.rgb_red = clamp_percent(data[APRILTAG_DATA_RED]);
    g_preferences.rgb_green = clamp_percent(data[APRILTAG_DATA_GREEN]);
    g_preferences.rgb_blue = clamp_percent(data[APRILTAG_DATA_BLUE]);
    memcpy(g_selected, data + APRILTAG_DATA_SELECTED, sizeof(g_selected));
    g_selected[APRILTAG_SELECTED_BYTES - 1U] &= 0x07U;
}

void apriltag_settings_apply_session(void)
{
    camera_session_preferences_t session;

    apriltag_settings_load();
    session.fps_enabled = g_preferences.fps_enabled;
    session.light_mode = (camera_light_mode_t)g_preferences.light_mode;
    session.rgb_red = g_preferences.rgb_red;
    session.rgb_green = g_preferences.rgb_green;
    session.rgb_blue = g_preferences.rgb_blue;
    camera_session_preferences_override(&session);
    apriltag_detector_set_refine_edges(g_preferences.refine_edges);
    camera_light_apply();
}

const apriltag_preferences_t *apriltag_settings_preferences(void)
{
    apriltag_settings_load();
    return &g_preferences;
}

uint8_t apriltag_settings_selected(uint16_t id)
{
    apriltag_settings_load();
    if(id >= APRILTAG_FAMILY_ID_COUNT)
        return 0U;
    return (g_selected[id >> 3U] & (uint8_t)(1U << (id & 7U))) ? 1U : 0U;
}

uint8_t apriltag_settings_toggle_selected(uint16_t id)
{
    uint8_t mask;

    apriltag_settings_load();
    if(id >= APRILTAG_FAMILY_ID_COUNT)
        return 0U;
    mask = (uint8_t)(1U << (id & 7U));
    g_selected[id >> 3U] ^= mask;
    apriltag_settings_save();
    return (g_selected[id >> 3U] & mask) ? 1U : 0U;
}

uint8_t apriltag_settings_clear_selected(void)
{
    apriltag_settings_load();
    if(apriltag_settings_selected_count() == 0U)
        return 0U;
    memset(g_selected, 0, sizeof(g_selected));
    apriltag_settings_save();
    return 1U;
}

uint16_t apriltag_settings_selected_count(void)
{
    uint16_t count = 0U;

    apriltag_settings_load();
    for(uint16_t id = 0U; id < APRILTAG_FAMILY_ID_COUNT; id++)
        count += apriltag_settings_selected(id);
    return count;
}

uint8_t apriltag_settings_set_refine(uint8_t enabled)
{
    apriltag_settings_load();
    enabled = enabled ? 1U : 0U;
    if(g_preferences.refine_edges == enabled)
        return 0U;
    g_preferences.refine_edges = enabled;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_output(apriltag_output_mode_t mode)
{
    apriltag_settings_load();
    mode = mode == APRILTAG_OUTPUT_SELECTED ? APRILTAG_OUTPUT_SELECTED : APRILTAG_OUTPUT_ALL;
    if(g_preferences.output_mode == mode)
        return 0U;
    g_preferences.output_mode = mode;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_fps(uint8_t enabled)
{
    apriltag_settings_load();
    enabled = enabled ? 1U : 0U;
    if(g_preferences.fps_enabled == enabled)
        return 0U;
    g_preferences.fps_enabled = enabled;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_far_search(uint8_t enabled)
{
    apriltag_settings_load();
    enabled = enabled != 0U;
    if(g_preferences.far_search == enabled)
        return 0U;
    g_preferences.far_search = enabled;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_light(uint8_t mode)
{
    apriltag_settings_load();
    mode = mode == CAMERA_LIGHT_RGB ? CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    if(g_preferences.light_mode == mode)
        return 0U;
    g_preferences.light_mode = mode;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_rgb(uint8_t channel, uint8_t value)
{
    uint8_t *target = channel == 0U ? &g_preferences.rgb_red :
                      (channel == 1U ? &g_preferences.rgb_green : &g_preferences.rgb_blue);

    apriltag_settings_load();
    value = clamp_percent(value);
    if(*target == value)
        return 0U;
    *target = value;
    apriltag_settings_save();
    return 1U;
}

const char *apriltag_settings_output_label(void)
{
    return apriltag_settings_preferences()->output_mode == APRILTAG_OUTPUT_SELECTED ?
           "SELECTED" : "ALL";
}
