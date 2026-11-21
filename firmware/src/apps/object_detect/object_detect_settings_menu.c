#include "object_detect_settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "../../core/camera_types.h"
#include "../../services/object_detect_config.h"
#include "object_detect_labels.h"
#include "object_detect_settings.h"

static const char *const g_light_choices[] = {"LED", "RGB"};

enum
{
    PAGE_DETECTION = OBJECT_DETECT_SETTINGS_CLASSES + 1U,
    PAGE_LIGHTS,
};

static const settings_menu_item_t g_detection_items[] = {
    {.id = OBJECT_DETECT_SETTINGS_CONFIDENCE, .title = "Confidence",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 10, .maximum = 90, .step = 5, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_NMS, .title = "NMS",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 90, .step = 5, .flags = SETTINGS_MENU_ITEM_WRAP},
};

static const settings_menu_item_t g_lights_items[] = {
    {.id = OBJECT_DETECT_SETTINGS_LIGHT, .title = "Source",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_light_choices, .choice_count = 2U,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_RGB_RED, .title = "RGB Red",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_RGB_GREEN, .title = "RGB Green",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_RGB_BLUE, .title = "RGB Blue",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10,
     .flags = SETTINGS_MENU_ITEM_WRAP},
};

static int32_t object_menu_read(void *context, uint16_t id)
{
    const object_detect_preferences_t *preferences =
        object_detect_settings_preferences();

    (void)context;
    if(id == OBJECT_DETECT_SETTINGS_CONFIDENCE)
        return preferences->confidence;
    if(id == OBJECT_DETECT_SETTINGS_NMS)
        return preferences->nms;
    if(id == OBJECT_DETECT_SETTINGS_FPS)
        return preferences->fps_enabled;
    if(id == OBJECT_DETECT_SETTINGS_LIGHT)
        return preferences->light_mode;
    if(id == OBJECT_DETECT_SETTINGS_RGB_RED)
        return preferences->rgb_red;
    if(id == OBJECT_DETECT_SETTINGS_RGB_GREEN)
        return preferences->rgb_green;
    if(id == OBJECT_DETECT_SETTINGS_RGB_BLUE)
        return preferences->rgb_blue;
    return 0;
}

static uint8_t object_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == OBJECT_DETECT_SETTINGS_CONFIDENCE)
        return object_detect_settings_set_confidence((uint8_t)value);
    if(id == OBJECT_DETECT_SETTINGS_NMS)
        return object_detect_settings_set_nms((uint8_t)value);
    if(id == OBJECT_DETECT_SETTINGS_FPS)
        return object_detect_settings_set_fps((uint8_t)value);
    if(id == OBJECT_DETECT_SETTINGS_LIGHT)
        return object_detect_settings_set_light((uint8_t)value);
    if(id >= OBJECT_DETECT_SETTINGS_RGB_RED &&
       id <= OBJECT_DETECT_SETTINGS_RGB_BLUE)
        return object_detect_settings_set_rgb(
            (uint8_t)(id - OBJECT_DETECT_SETTINGS_RGB_RED), (uint8_t)value);
    return 0U;
}

static uint8_t object_menu_format(void *context,
                                  uint16_t id,
                                  char *value,
                                  size_t value_size)
{
    (void)context;
    if(id == OBJECT_DETECT_SETTINGS_CLASSES)
    {
        uint32_t mask = object_detect_settings_class_mask();
        unsigned count = 0U;

        for(; mask; mask >>= 1U)
            count += mask & 1U;
        snprintf(value, value_size, "%u/%u >", count, OBJECT_DETECT_CLASS_COUNT);
        return 1U;
    }
    if(id == OBJECT_DETECT_SETTINGS_VERSION)
    {
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
        return 1U;
    }
    return 0U;
}

static void object_menu_changed(void *context, uint16_t id)
{
    (void)context;
    (void)id;
    object_detect_settings_apply_session();
}

enum
{
    CLASS_MENU_ENABLE_ALL = OBJECT_DETECT_CLASS_COUNT,
    CLASS_MENU_DISABLE_ALL,
};

static const settings_menu_item_t g_class_items[] = {
    {.id = CLASS_MENU_ENABLE_ALL, .title = "Enable All", .kind = SETTINGS_MENU_ITEM_ACTION,
     .flags = SETTINGS_MENU_ITEM_REDRAW_PAGE},
    {.id = CLASS_MENU_DISABLE_ALL, .title = "Disable All", .kind = SETTINGS_MENU_ITEM_ACTION,
     .flags = SETTINGS_MENU_ITEM_REDRAW_PAGE},
#define CLASS_TOGGLE(class_id, label) \
    {.id = class_id, .title = label, .kind = SETTINGS_MENU_ITEM_TOGGLE},
    OBJECT_DETECT_VOC20_LABELS(CLASS_TOGGLE)
#undef CLASS_TOGGLE
};

_Static_assert(sizeof(g_class_items) / sizeof(g_class_items[0]) ==
               OBJECT_DETECT_CLASS_COUNT + 2U, "Missing class filter rows");

static int32_t class_menu_read(void *context, uint16_t id)
{
    (void)context;
    return id < OBJECT_DETECT_CLASS_COUNT &&
           (object_detect_settings_class_mask() & (UINT32_C(1) << id));
}

static uint8_t class_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id >= OBJECT_DETECT_CLASS_COUNT)
        return 0U;

    uint32_t mask = object_detect_settings_class_mask();
    uint32_t bit = UINT32_C(1) << id;
    return object_detect_settings_set_class_mask(value ? (mask | bit) : (mask & ~bit));
}

static uint8_t class_menu_action(void *context, uint16_t id)
{
    (void)context;
    return object_detect_settings_set_class_mask(
        id == CLASS_MENU_ENABLE_ALL ? OBJECT_DETECT_ALL_CLASSES : 0U);
}

static const settings_menu_definition_t g_class_definition = {
    .title = "OBJ CLASSES",
    .items = g_class_items,
    .item_count = (uint8_t)(sizeof(g_class_items) / sizeof(g_class_items[0])),
    .read = class_menu_read,
    .write = class_menu_write,
    .action = class_menu_action,
};

#define OBJECT_PAGE(title_, items_) \
    { .title = title_, .items = items_, .item_count = sizeof(items_) / sizeof((items_)[0]), \
      .read = object_menu_read, .write = object_menu_write, \
      .format = object_menu_format, .changed = object_menu_changed }

static const settings_menu_definition_t g_detection =
    OBJECT_PAGE("OBJ DETECTION", g_detection_items);
static const settings_menu_definition_t g_lights =
    OBJECT_PAGE("OBJ LIGHTS", g_lights_items);

static const settings_menu_item_t g_root_items[] = {
    {.id = PAGE_DETECTION, .title = "Detection", .kind = SETTINGS_MENU_ITEM_PAGE,
     .page = &g_detection},
    {.id = OBJECT_DETECT_SETTINGS_CLASSES, .title = "Classes", .kind = SETTINGS_MENU_ITEM_PAGE,
     .page = &g_class_definition},
    {.id = PAGE_LIGHTS, .title = "Lights", .kind = SETTINGS_MENU_ITEM_PAGE,
     .page = &g_lights},
    {.id = OBJECT_DETECT_SETTINGS_FPS, .title = "FPS Counter", .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = OBJECT_DETECT_SETTINGS_VERSION, .title = "Version", .kind = SETTINGS_MENU_ITEM_TEXT},
};

static const settings_menu_definition_t g_definition =
    OBJECT_PAGE("OBJ SETTINGS", g_root_items);

const settings_menu_definition_t *object_detect_settings_menu_definition(void)
{
    return &g_definition;
}
