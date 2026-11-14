#include "apriltag_settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "apriltag_config.h"
#include "apriltag_controller.h"
#include "apriltag_distance.h"
#include "apriltag_settings.h"
#include "../../core/camera_types.h"

static const char *const g_output_choices[] = {"ALL", "SELECTED"};
static const char *const g_search_choices[] = {"NEAR", "FAR"};
static const char *const g_light_choices[] = {"LED", "RGB"};

enum
{
    ITEM_DISTANCE = 16,
    ITEM_TAG_MM,
    ITEM_REF_MM,
    ITEM_CALIBRATE,
    ITEM_FOCAL,
    ITEM_LEVEL,
    ITEM_UPRIGHT,
    ITEM_LENS_DOWN,
    LEVEL_ROWS = 3,
};

static const settings_menu_item_t g_items[] = {
    {.id = APRILTAG_SETTINGS_SEARCH, .title = "Search",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_search_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = ITEM_DISTANCE, .title = "Distance", .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = ITEM_TAG_MM, .title = "Tag mm", .kind = SETTINGS_MENU_ITEM_RANGE,
     .interaction = SETTINGS_MENU_EDIT_ON_OK, .minimum = 20, .maximum = 150, .step = 1},
    {.id = ITEM_REF_MM, .title = "Ref mm", .kind = SETTINGS_MENU_ITEM_RANGE,
     .interaction = SETTINGS_MENU_EDIT_ON_OK, .minimum = 100, .maximum = 1000, .step = 10},
    {.id = ITEM_CALIBRATE, .title = "Calibrate", .kind = SETTINGS_MENU_ITEM_ACTION},
    {.id = ITEM_FOCAL, .title = "Focal", .kind = SETTINGS_MENU_ITEM_RANGE,
     .interaction = SETTINGS_MENU_EDIT_ON_OK, .minimum = APRILTAG_DISTANCE_FOCAL_MIN_MPX / 10U,
     .maximum = APRILTAG_DISTANCE_FOCAL_MAX_MPX / 10U, .step = 1,
     .edit_initial = 12000,
     .flags = SETTINGS_MENU_ITEM_CENTIPIXELS | SETTINGS_MENU_ITEM_ACCELERATE},
    {.id = ITEM_LEVEL, .title = "Level range", .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = ITEM_UPRIGHT, .title = "Set upright", .kind = SETTINGS_MENU_ITEM_ACTION},
    {.id = ITEM_LENS_DOWN, .title = "Set lens down", .kind = SETTINGS_MENU_ITEM_ACTION},
    {.id = APRILTAG_SETTINGS_REFINE, .title = "Refine Edges",
     .kind = SETTINGS_MENU_ITEM_TOGGLE, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = APRILTAG_SETTINGS_OUTPUT, .title = "Output",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_output_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_CLEAR_IDS, .title = "Clear IDs",
     .kind = SETTINGS_MENU_ITEM_ACTION, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = APRILTAG_SETTINGS_FPS, .title = "FPS Counter",
     .kind = SETTINGS_MENU_ITEM_TOGGLE, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = APRILTAG_SETTINGS_LIGHT, .title = "Light",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_light_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_RGB_RED, .title = "RGB Red",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_RGB_GREEN, .title = "RGB Green",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_RGB_BLUE, .title = "RGB Blue",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_VERSION, .title = "Version",
     .kind = SETTINGS_MENU_ITEM_TEXT, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
};

static int32_t apriltag_menu_read(void *context, uint16_t id)
{
    const apriltag_preferences_t *preferences = apriltag_settings_preferences();

    (void)context;
    const apriltag_distance_settings_t *distance = apriltag_settings_distance();
    switch(id)
    {
    case APRILTAG_SETTINGS_SEARCH: return preferences->far_search;
    case ITEM_DISTANCE: return distance->enabled;
    case ITEM_TAG_MM: return distance->tag_mm;
    case ITEM_REF_MM: return distance->reference_mm;
    case ITEM_FOCAL: return (int32_t)(distance->focal_millipixels / 10U);
    case ITEM_LEVEL: return distance->level;
    default: break;
    }
    if(id == APRILTAG_SETTINGS_REFINE)
        return preferences->refine_edges;
    if(id == APRILTAG_SETTINGS_OUTPUT)
        return preferences->output_mode;
    if(id == APRILTAG_SETTINGS_CLEAR_IDS)
        return apriltag_settings_selected_count();
    if(id == APRILTAG_SETTINGS_FPS)
        return preferences->fps_enabled;
    if(id == APRILTAG_SETTINGS_LIGHT)
        return preferences->light_mode;
    if(id == APRILTAG_SETTINGS_RGB_RED)
        return preferences->rgb_red;
    if(id == APRILTAG_SETTINGS_RGB_GREEN)
        return preferences->rgb_green;
    if(id == APRILTAG_SETTINGS_RGB_BLUE)
        return preferences->rgb_blue;
    return 0;
}

static uint8_t apriltag_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    apriltag_distance_settings_t *distance = apriltag_settings_distance();
    switch(id)
    {
    case APRILTAG_SETTINGS_SEARCH:
        return apriltag_settings_set_far_search((uint8_t)value);
    case ITEM_DISTANCE:
        distance->enabled = (uint8_t)value;
        break;
    case ITEM_TAG_MM:
        distance->tag_mm = (uint16_t)value;
        break;
    case ITEM_REF_MM:
        distance->reference_mm = (uint16_t)value;
        break;
    case ITEM_FOCAL:
        distance->focal_millipixels = (uint32_t)value * 10U;
        apriltag_controller_cancel_calibration();
        break;
    case ITEM_LEVEL:
        distance->level = (uint8_t)value;
        break;
    default:
        if(id == APRILTAG_SETTINGS_REFINE)
            return apriltag_settings_set_refine((uint8_t)value);
        if(id == APRILTAG_SETTINGS_OUTPUT)
            return apriltag_settings_set_output((apriltag_output_mode_t)value);
        if(id == APRILTAG_SETTINGS_FPS)
            return apriltag_settings_set_fps((uint8_t)value);
        if(id == APRILTAG_SETTINGS_LIGHT)
            return apriltag_settings_set_light((uint8_t)value);
        if(id >= APRILTAG_SETTINGS_RGB_RED && id <= APRILTAG_SETTINGS_RGB_BLUE)
            return apriltag_settings_set_rgb((uint8_t)(id - APRILTAG_SETTINGS_RGB_RED),
                                             (uint8_t)value);
        return 0U;
    }
    apriltag_settings_save_distance();
    return 1U;
}

static uint8_t apriltag_menu_action(void *context, uint16_t id)
{
    (void)context;
    switch(id)
    {
    case APRILTAG_SETTINGS_CLEAR_IDS: return apriltag_settings_clear_selected();
    case ITEM_CALIBRATE: return apriltag_controller_arm_calibration();
    case ITEM_UPRIGHT: return apriltag_controller_capture_gravity(0U);
    case ITEM_LENS_DOWN: return apriltag_controller_capture_gravity(1U);
    default: return 0U;
    }
}

static uint8_t apriltag_menu_format(void *context,
                                    uint16_t id,
                                    char *value,
                                    size_t value_size)
{
    (void)context;
    const apriltag_distance_settings_t *distance = apriltag_settings_distance();
    if(id == ITEM_FOCAL && !distance->focal_millipixels)
    {
        snprintf(value, value_size, "UNSET");
        return 1U;
    }
    if(id == ITEM_CALIBRATE)
    {
        snprintf(value, value_size, "%s", !distance->enabled ? "OFF" :
                 apriltag_controller_calibration_armed() ? "ARMED" :
                 distance->focal_millipixels ? "SET" : "START");
        return 1U;
    }
    if(id == ITEM_UPRIGHT || id == ITEM_LENS_DOWN)
    {
        const int16_t *sample = id == ITEM_UPRIGHT ? distance->upright : distance->lens_down;
        snprintf(value, value_size, "%s", sample[0] || sample[1] || sample[2] ? "SET" : "-");
        return 1U;
    }
    if(id == APRILTAG_SETTINGS_CLEAR_IDS)
    {
        snprintf(value, value_size, "%u", (unsigned)apriltag_settings_selected_count());
        return 1U;
    }
    if(id == APRILTAG_SETTINGS_VERSION)
    {
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
        return 1U;
    }
    return 0U;
}

static void apriltag_menu_changed(void *context, uint16_t id)
{
    (void)context;
    if(id == APRILTAG_SETTINGS_REFINE || id == APRILTAG_SETTINGS_FPS ||
       id == APRILTAG_SETTINGS_LIGHT ||
       (id >= APRILTAG_SETTINGS_RGB_RED && id <= APRILTAG_SETTINGS_RGB_BLUE))
        apriltag_settings_apply_session();
    if(id == ITEM_DISTANCE || id == ITEM_FOCAL)
        settings_menu_redraw_all(context);
}

static const settings_menu_definition_t g_definition = {
    .title = "TAG SETTINGS",
    .items = g_items,
    .item_count = (uint8_t)(sizeof(g_items) / sizeof(g_items[0])),
    .context = NULL,
    .read = apriltag_menu_read,
    .write = apriltag_menu_write,
    .action = apriltag_menu_action,
    .format = apriltag_menu_format,
    .changed = apriltag_menu_changed,
    .defer_edits = 1U,
};

const settings_menu_definition_t *apriltag_settings_menu_definition(settings_menu_session_t *session)
{
    static settings_menu_definition_t definition;
    static settings_menu_item_t optical_items[sizeof(g_items) / sizeof(g_items[0]) - LEVEL_ROWS];
    definition = g_definition;
    definition.context = session;
    if(!apriltag_controller_has_accelerometer())
    {
        uint8_t count = 0U;
        for(unsigned i = 0U; i < g_definition.item_count; i++)
            if(g_items[i].id < ITEM_LEVEL || g_items[i].id > ITEM_LENS_DOWN)
                optical_items[count++] = g_items[i];
        definition.items = optical_items;
        definition.item_count = count;
    }
    return &definition;
}
