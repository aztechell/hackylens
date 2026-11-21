#ifndef HK_MENU_ORDER_H
#define HK_MENU_ORDER_H

#include <stdint.h>

/* The full launcher has 16 built-in icons and up to 8 script shortcuts. */
#define HK_MENU_ORDER_CAPACITY 24U
#define HK_MENU_SHORTCUT_CAPACITY 8U
#define HK_MENU_ORDER_NAME_SIZE 64U

enum
{
    HK_MENU_ORDER_APP = 1,
    HK_MENU_ORDER_SHORTCUT,
};

typedef struct
{
    uint8_t kind;
    char name[HK_MENU_ORDER_NAME_SIZE];
} hk_menu_order_key_t;

typedef struct
{
    hk_menu_order_key_t keys[HK_MENU_ORDER_CAPACITY];
} hk_menu_order_t;

#endif
