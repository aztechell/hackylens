#include "settings_menu.h"

#include <stdio.h>
#include <string.h>

#include "settings_firmware.h"

typedef enum
{
    ITEM_LED_SWITCH = 8,
    ITEM_LED_BRIGHTNESS,
    ITEM_RGB_SWITCH,
    ITEM_BRIGHTNESS,
    ITEM_AUTO_SLEEP,
    ITEM_CONNECTION,
    ITEM_UART_SPEED,
    ITEM_AUTOSTART,
    ITEM_BOARD,
    ITEM_VERSION,
    ITEM_SD,
    ITEM_VOLUME,
    ITEM_UI_COLOR,
    ITEM_SD_USED,
    ITEM_SD_TOTAL,
} settings_item_t;

static const char *const transport_choices[] = {"UART", "I2C"};
static const char *const grove_baud_choices[] = {"9600", "115200", "1000000"};
static const char *const ui_color_choices[HK_UI_COLOR_COUNT] = {
    [HK_UI_COLOR_GREEN] = "Green",
    [HK_UI_COLOR_CYAN] = "Cyan",
    [HK_UI_COLOR_AMBER] = "Amber",
    [HK_UI_COLOR_PINK] = "Pink",
    [HK_UI_COLOR_BLUE] = "Blue",
    [HK_UI_COLOR_WHITE] = "White",
};

#define PAGE(id_, label_) { .id = id_, .title = label_, .kind = SETTINGS_MENU_ITEM_ACTION }
#define TOGGLE(id_, label_) { .id = id_, .title = label_, .kind = SETTINGS_MENU_ITEM_TOGGLE }
#define RANGE(id_, label_, min_, max_, step_, format_) \
    { .id = id_, .title = label_, .kind = SETTINGS_MENU_ITEM_RANGE, \
      .interaction = SETTINGS_MENU_EDIT_ON_OK, .minimum = min_, .maximum = max_, \
      .step = step_, .flags = format_ }
#define CHOICE(id_, label_, choices_, count_) \
    { .id = id_, .title = label_, .kind = SETTINGS_MENU_ITEM_CHOICE, \
      .interaction = SETTINGS_MENU_EDIT_ON_OK, .choices = choices_, \
      .choice_count = count_, .flags = SETTINGS_MENU_ITEM_WRAP }
#define INFO(id_, label_) { .id = id_, .title = label_, .kind = SETTINGS_MENU_ITEM_TEXT }

static const settings_menu_item_t root_items[] = {
    PAGE(SETTINGS_PAGE_SCREEN, "Screen"),
    PAGE(SETTINGS_PAGE_LIGHTS, "Lights"),
    PAGE(SETTINGS_PAGE_CONNECTION, "Connection"),
    PAGE(SETTINGS_PAGE_STARTUP, "Startup"),
    PAGE(SETTINGS_PAGE_DEVICE, "Device"),
    PAGE(SETTINGS_PAGE_SOUND, "Sound"),
};

static const settings_menu_item_t screen_items[] = {
    RANGE(ITEM_BRIGHTNESS, "Brightness", 10, 100, 10, SETTINGS_MENU_ITEM_PERCENT),
    CHOICE(ITEM_UI_COLOR, "UI Color", ui_color_choices, HK_UI_COLOR_COUNT),
    RANGE(ITEM_AUTO_SLEEP, "Sleep", 0, 30, 1, SETTINGS_MENU_ITEM_MINUTES),
};

static const settings_menu_item_t lights_items[] = {
    TOGGLE(ITEM_LED_SWITCH, "Camera LED"),
    RANGE(ITEM_LED_BRIGHTNESS, "LED Level", 0, 100, 10, SETTINGS_MENU_ITEM_PERCENT),
    TOGGLE(ITEM_RGB_SWITCH, "RGB LED"),
    PAGE(SETTINGS_PAGE_RGB, "RGB Color"),
};

static const settings_menu_item_t rgb_items[] = {
    RANGE(SETTINGS_RGB_RED, "Red", 0, 100, 10, SETTINGS_MENU_ITEM_PERCENT),
    RANGE(SETTINGS_RGB_GREEN, "Green", 0, 100, 10, SETTINGS_MENU_ITEM_PERCENT),
    RANGE(SETTINGS_RGB_BLUE, "Blue", 0, 100, 10, SETTINGS_MENU_ITEM_PERCENT),
};

static const settings_menu_item_t connection_items[] = {
    CHOICE(ITEM_CONNECTION, "Grove Mode", transport_choices, 2U),
    CHOICE(ITEM_UART_SPEED, "Baud", grove_baud_choices, EXTERNAL_LINK_UART_SPEED_COUNT),
};

static const settings_menu_item_t startup_items[] = {
    CHOICE(ITEM_AUTOSTART, "Autostart", NULL, 0U),
};

static const settings_menu_item_t device_items[] = {
    INFO(ITEM_BOARD, "Board"),
    INFO(ITEM_VERSION, "Firmware"),
    INFO(ITEM_SD, "microSD"),
    INFO(ITEM_SD_USED, "Used"),
    INFO(ITEM_SD_TOTAL, "Total"),
};

static const settings_menu_item_t sound_items[] = {
    RANGE(ITEM_VOLUME, "Volume", 0, 100, 10, SETTINGS_MENU_ITEM_PERCENT),
};

static int32_t autostart_read(const settings_startup_choices_t *choices)
{
    hk_autostart_id_t current = settings_autostart_id();
    if(current == HK_AUTOSTART_OFF)
        return 0;
    uint8_t native_count = hk_app_autostart_count();
    for(uint8_t i = 0; i < native_count; i++)
        if(hk_app_autostart_at(i)->autostart_id == current)
            return 1 + i;
#if HK_ENABLE_APP_MICROPYTHON
    char name[USERFS_NAME_MAX + 1U];
    if(choices && current == HK_AUTOSTART_PINNED_SCRIPT &&
       userfs_get_boot_shortcut(name, sizeof(name)) == USERFS_OK)
    {
        for(uint8_t i = 0; i < choices->count; i++)
            if(strcmp(choices->names[i], name) == 0)
                return 1 + native_count + i;
    }
#else
    (void)choices;
#endif
    return -1;
}

static int32_t settings_app_read(void *context, uint16_t id)
{
    switch(id)
    {
    case SETTINGS_RGB_RED: return settings_rgb_red();
    case SETTINGS_RGB_GREEN: return settings_rgb_green();
    case SETTINGS_RGB_BLUE: return settings_rgb_blue();
    case ITEM_LED_SWITCH: return settings_led_enabled();
    case ITEM_LED_BRIGHTNESS: return settings_led_brightness();
    case ITEM_RGB_SWITCH: return settings_rgb_enabled();
    case ITEM_BRIGHTNESS: return settings_screen_brightness();
    case ITEM_UI_COLOR: return settings_ui_color();
    case ITEM_AUTO_SLEEP: return settings_auto_sleep_minutes();
    case ITEM_CONNECTION: return settings_external_link_transport();
    case ITEM_UART_SPEED: return settings_external_link_uart_speed();
    case ITEM_AUTOSTART: return autostart_read(context);
    case ITEM_VOLUME: return settings_audio_volume();
    default: return 0;
    }
}

static uint8_t autostart_write(const settings_startup_choices_t *choices, int32_t value)
{
    uint8_t native_count = hk_app_autostart_count();
    if(value == 0)
        settings_set_autostart_id(HK_AUTOSTART_OFF);
    else if(value > 0 && value <= native_count)
        settings_set_autostart_id(hk_app_autostart_at((uint8_t)value - 1U)->autostart_id);
#if HK_ENABLE_APP_MICROPYTHON
    else if(choices && value > native_count && value <= native_count + choices->count)
    {
        const char *name = choices->names[value - native_count - 1];
        if(userfs_set_boot_shortcut(name) != USERFS_OK)
            return 0U;
        settings_set_autostart_id(HK_AUTOSTART_PINNED_SCRIPT);
    }
#endif
    else
        return 0U;
#if !HK_ENABLE_APP_MICROPYTHON
    (void)choices;
#endif
    return 1U;
}

static uint8_t settings_app_write(void *context, uint16_t id, int32_t value)
{
    switch(id)
    {
    case SETTINGS_RGB_RED: settings_set_rgb_red((uint8_t)value); rgb_led_apply(); break;
    case SETTINGS_RGB_GREEN: settings_set_rgb_green((uint8_t)value); rgb_led_apply(); break;
    case SETTINGS_RGB_BLUE: settings_set_rgb_blue((uint8_t)value); rgb_led_apply(); break;
    case ITEM_LED_SWITCH: settings_set_led_enabled((uint8_t)value); illum_led_apply(); break;
    case ITEM_LED_BRIGHTNESS: settings_set_led_brightness((uint8_t)value); illum_led_apply(); break;
    case ITEM_RGB_SWITCH: settings_set_rgb_enabled((uint8_t)value); rgb_led_apply(); break;
    case ITEM_BRIGHTNESS: settings_set_screen_brightness((uint8_t)value); screen_brightness_apply(); break;
    case ITEM_UI_COLOR: settings_set_ui_color((uint8_t)value); break;
    case ITEM_AUTO_SLEEP: settings_set_auto_sleep_minutes((uint8_t)value); activity_note(); break;
    case ITEM_CONNECTION:
        settings_set_external_link_transport((external_link_transport_t)value);
        external_link_service_set_transport(settings_external_link_transport());
        break;
    case ITEM_UART_SPEED:
        settings_set_external_link_uart_speed((external_link_uart_speed_t)value);
        external_link_service_set_uart_baud(settings_external_link_uart_baud());
        break;
    case ITEM_AUTOSTART:
        if(!autostart_write(context, value))
            return 0U;
        break;
    case ITEM_VOLUME:
        if(audio_service_set_volume((uint8_t)value) != HK_OK)
            return 0U;
        break;
    default: return 0U;
    }
    settings_mark_dirty(0U);
    return 1U;
}

static void settings_app_preview(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == ITEM_BRIGHTNESS)
        settings_backlight_preview((uint8_t)value);
    else if(id == ITEM_UI_COLOR)
        hk_ui_preview_color((uint8_t)value);
    else if(id == ITEM_LED_BRIGHTNESS && settings_led_enabled())
        settings_illumination_preview((uint8_t)value);
    else if(id >= SETTINGS_RGB_RED && id <= SETTINGS_RGB_BLUE && settings_rgb_enabled())
        settings_rgb_preview(id == SETTINGS_RGB_RED ? (uint8_t)value : settings_rgb_red(),
                             id == SETTINGS_RGB_GREEN ? (uint8_t)value : settings_rgb_green(),
                             id == SETTINGS_RGB_BLUE ? (uint8_t)value : settings_rgb_blue());
}

static void format_capacity(uint64_t bytes, char *text, size_t size)
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    uint64_t unit = 1U;
    unsigned index = 0U;
    while(bytes >= unit * 1024U && index + 1U < sizeof(units) / sizeof(units[0]))
    {
        unit *= 1024U;
        index++;
    }
    if(index == 0U)
        snprintf(text, size, "%u B", (unsigned)bytes);
    else
    {
        unsigned tenths = (unsigned)(bytes * 10U / unit);
        snprintf(text, size, "%u.%u %s", tenths / 10U, tenths % 10U, units[index]);
    }
}

static uint8_t settings_app_format(void *context, uint16_t id, char *value, size_t size)
{
    const char *label;
    switch(id)
    {
    case SETTINGS_PAGE_SCREEN:
        snprintf(value, size, "%u%% >", (unsigned)settings_screen_brightness());
        return 1U;
    case SETTINGS_PAGE_SOUND:
        snprintf(value, size, "%u%% >", (unsigned)settings_audio_volume());
        return 1U;
    case SETTINGS_PAGE_LIGHTS:
    case SETTINGS_PAGE_CONNECTION:
    case SETTINGS_PAGE_STARTUP:
    case SETTINGS_PAGE_DEVICE:
    case SETTINGS_PAGE_RGB:
        label = ">";
        break;
    case ITEM_BOARD:
        label = settings_board_label();
        break;
    case ITEM_VERSION:
        label = "v" HACKYLENS_VERSION;
        break;
    case ITEM_SD:
        label = !hk_sd_present() ? "NO CARD" : hk_fat_mounted() ? "FAT32" : "ERROR";
        break;
    case ITEM_SD_USED:
    case ITEM_SD_TOTAL:
    {
        fat32_usage_t usage = fat32_usage();
        if(usage.status == FAT32_USAGE_READY ||
           (id == ITEM_SD_TOTAL && usage.status != FAT32_USAGE_UNAVAILABLE))
        {
            format_capacity(id == ITEM_SD_USED ? usage.used_bytes : usage.total_bytes,
                            value, size);
            return 1U;
        }
        if(usage.status == FAT32_USAGE_SCANNING)
        {
            unsigned progress = usage.progress / SETTINGS_SD_PROGRESS_STEP * SETTINGS_SD_PROGRESS_STEP;
            snprintf(value, size, "SCAN %u%%", progress);
            return 1U;
        }
        label = usage.status == FAT32_USAGE_ERROR ? "READ ERROR" : "-";
        break;
    }
    case ITEM_AUTOSTART:
        if(autostart_read(context) >= 0)
            return 0U;
        label = "Unavailable";
        break;
    default:
        return 0U;
    }
    snprintf(value, size, "%s", label);
    return 1U;
}

static uint8_t settings_app_choice_count(void *context, uint16_t id)
{
    if(id != ITEM_AUTOSTART)
        return 0U;
    uint8_t count = (uint8_t)(1U + hk_app_autostart_count());
    const settings_startup_choices_t *choices = context;
    if(choices)
        count += choices->count;
    return count;
}

static const char *settings_app_choice_label(void *context, uint16_t id, uint8_t index)
{
    if(id != ITEM_AUTOSTART)
        return NULL;
    if(index == 0U)
        return "OFF";
    uint8_t native_count = hk_app_autostart_count();
    if(index <= native_count)
        return hk_app_autostart_at(index - 1U)->title;
    const settings_startup_choices_t *choices = context;
    uint8_t pin_index = index - native_count - 1U;
    return choices && pin_index < choices->count ? choices->names[pin_index] : NULL;
}

static void settings_app_committed(void *context, uint16_t id)
{
    (void)context;
    (void)id;
    settings_storage_save_now();
}

#define DEFINITION(title_, items_, count_) \
    { .title = title_, .items = items_, .item_count = count_, \
      .read = settings_app_read, .write = settings_app_write, \
      .format = settings_app_format, .choice_count = settings_app_choice_count, \
      .choice_label = settings_app_choice_label, .committed = settings_app_committed, \
      .preview = settings_app_preview, .defer_edits = 1U }
#define FULL_PAGE(title_, items_) DEFINITION(title_, items_, sizeof(items_) / sizeof((items_)[0]))

static const settings_menu_definition_t pages[] = {
    [SETTINGS_PAGE_ROOT] = FULL_PAGE("SETTINGS", root_items),
    [SETTINGS_PAGE_SCREEN] = FULL_PAGE("SCREEN", screen_items),
    [SETTINGS_PAGE_LIGHTS] = FULL_PAGE("LIGHTS", lights_items),
    [SETTINGS_PAGE_CONNECTION] = FULL_PAGE("CONNECTION", connection_items),
    [SETTINGS_PAGE_STARTUP] = FULL_PAGE("STARTUP", startup_items),
    [SETTINGS_PAGE_DEVICE] = FULL_PAGE("DEVICE", device_items),
    [SETTINGS_PAGE_SOUND] = FULL_PAGE("SOUND", sound_items),
    [SETTINGS_PAGE_RGB] = FULL_PAGE("RGB COLOR", rgb_items),
};

static const settings_menu_definition_t two_wire_page = DEFINITION("CONNECTION", connection_items, 1U);
static const settings_menu_definition_t root_without_sound =
    DEFINITION("SETTINGS", root_items, sizeof(root_items) / sizeof(root_items[0]) - 1U);

const settings_menu_definition_t *settings_app_menu_definition(void)
{
    return audio_service_available() ? &pages[SETTINGS_PAGE_ROOT] : &root_without_sound;
}

const settings_menu_definition_t *settings_app_page_definition(settings_page_t page)
{
    if(page == SETTINGS_PAGE_ROOT)
        return settings_app_menu_definition();
    if(page == SETTINGS_PAGE_CONNECTION && settings_external_link_transport() == EXTERNAL_LINK_I2C)
        return &two_wire_page;
    if((unsigned)page >= sizeof(pages) / sizeof(pages[0]) || !pages[page].item_count)
        return NULL;
    return &pages[page];
}

const settings_menu_definition_t *settings_app_startup_choices(settings_startup_choices_t *choices)
{
    choices->count = 0;
#if HK_ENABLE_APP_MICROPYTHON
    for(uint8_t i = 0, count = userfs_pin_count(); i < count; i++)
    {
        const char *name = userfs_pin_name(i);
        if(name)
            snprintf(choices->names[choices->count++], sizeof(choices->names[0]), "%s", name);
    }
#endif
    choices->definition = pages[SETTINGS_PAGE_STARTUP];
    choices->definition.context = choices;
    return &choices->definition;
}
