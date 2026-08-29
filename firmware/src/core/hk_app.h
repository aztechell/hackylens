#ifndef HK_APP_H
#define HK_APP_H

#include <stddef.h>
#include <stdint.h>

#include "hk_events.h"

typedef enum
{
    SCREEN_MENU = 0,
    SCREEN_CAMERA,
    SCREEN_QR_CAMERA,
    SCREEN_FACE_DETECT,
    SCREEN_APRILTAG,
    SCREEN_CAMERA_SETTINGS,
    SCREEN_FILES,
    SCREEN_BUTTONS,
    SCREEN_APP_SLOT_0,
    SCREEN_APP_SLOT_1,
    SCREEN_APP_SLOT_2,
    SCREEN_APP_SLOT_3,
    SCREEN_SETTINGS,
    SCREEN_SLEEP,
    SCREEN_OBJECT_DETECT,
} screen_t;

typedef uint16_t hk_autostart_id_t;

enum
{
    HK_AUTOSTART_OFF = 0,
};

typedef struct
{
    uint32_t state;
    uint32_t pressed;
    uint32_t changed;
} hk_input_snapshot_t;

typedef struct
{
    screen_t screen;
    void (*enter)(const hk_input_snapshot_t *input);
    void (*exit)(void);
    void (*tick)(const hk_input_snapshot_t *input);
    void (*handle_input)(const hk_input_snapshot_t *input);
    uint8_t (*owns_screen)(screen_t screen);
    void (*draw_icon)(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
    void (*background_tick)(const hk_input_snapshot_t *input);
    void (*handle_sd_event)(hk_sd_event_t event);
    uint8_t blocks_sd_poll;
    uint8_t (*handle_debug_command)(const char *cmd);
} hk_legacy_app_entry_t;

typedef struct hk_app_v2_entry hk_app_v2_entry_t;

typedef enum
{
    HK_APP_LIFECYCLE_LEGACY = 0,
    HK_APP_LIFECYCLE_V2 = 1,
} hk_app_lifecycle_kind_t;

typedef union
{
    const hk_legacy_app_entry_t *legacy;
    const hk_app_v2_entry_t *v2;
} hk_app_entry_t;

typedef struct
{
    const char *id;
    uint16_t instance;
    const char *minimum;
    const char *maximum_exclusive;
    const char *const *features;
    uint16_t feature_count;
    const char *fallback;
    uint8_t optional;
} hk_app_capability_request_t;

typedef struct
{
    const char *id;
    const char *namespace_name;
} hk_app_service_request_t;

typedef struct
{
    uint32_t static_ram_bytes;
    uint32_t stack_bytes;
    uint32_t state_bytes;
    uint32_t state_alignment;
    uint32_t tick_interval_us;
    uint32_t tick_budget_us;
    uint32_t render_budget_us;
} hk_app_limits_t;

/* Schema 1 keeps alignment as a runtime ABI policy, not a manifest knob. */
#define HK_APP_STATE_ALIGNMENT 16U

#define HK_APP_DESCRIPTOR_VERSION 1U

typedef struct hk_app
{
    uint16_t struct_size;
    uint16_t struct_version;
    const char *id;
    const char *title;
    const char *version;
    uint16_t menu_order;
    uint8_t menu_visible;
    hk_autostart_id_t autostart_id;
    uint8_t autostart_eligible;
    hk_app_lifecycle_kind_t lifecycle;
    hk_app_entry_t entry;
    const char *help;
    const char *debug_help;
    hk_app_limits_t limits;
    const hk_app_capability_request_t *capabilities;
    uint16_t capability_count;
    const hk_app_service_request_t *services;
    uint16_t service_count;
    screen_t screen;
    void (*handle_input)(const hk_input_snapshot_t *input);
} hk_app_t;

static inline const hk_legacy_app_entry_t *hk_app_legacy_entry(
    const hk_app_t *app)
{
    if(!app || app->lifecycle != HK_APP_LIFECYCLE_LEGACY)
        return NULL;
    return app->entry.legacy;
}

#endif
