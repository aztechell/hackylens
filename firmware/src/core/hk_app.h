#ifndef HK_APP_H
#define HK_APP_H

#include <stddef.h>
#include <stdint.h>

#include "hk_events.h"

typedef enum { SCREEN_MENU = 0, SCREEN_APP = 1 } screen_t;

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

typedef struct hk_app_v2_entry hk_app_v2_entry_t;

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

/* Private descriptor encoding; runtime asserts equality with the public SDK ABI. */
#define HK_APP_DESCRIPTOR_STATE_ALIGNMENT 16U

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
    const hk_app_v2_entry_t *entry;
    const char *help;
    const char *debug_help;
    hk_app_limits_t limits;
    uint8_t (*debug_command)(const char *command);
    void (*draw_icon)(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
} hk_app_t;

#endif
