#ifndef HK_DISPLAY_PROVIDER_H
#define HK_DISPLAY_PROVIDER_H

#include <hackylens/capability/display.h>

typedef struct
{
    const hk_display_t *claimants[2];
    uint32_t quarantined;
} hk_display_state_t;

struct hk_display_service
{
    hk_display_state_t *state;
    void *context;
    hk_result_t (*open_plane)(void *context, const hk_display_t *session,
                              uint32_t plane);
    hk_result_t (*close_plane)(void *context, const hk_display_t *session,
                               hk_deadline_t deadline);
    void (*retire_plane)(void *context, const hk_display_t *session);
    hk_result_t (*get_info)(void *context, hk_display_info_t *info);
    hk_result_t (*begin_batch)(void *context, const hk_display_t *session);
    hk_result_t (*set_clip)(void *context, const hk_display_t *session,
                            const hk_display_rect_t *clip);
    hk_result_t (*clear)(void *context, const hk_display_t *session,
                         uint16_t color);
    hk_result_t (*fill_rect)(void *context, const hk_display_t *session,
                             const hk_display_rect_t *rect, uint16_t color);
    hk_result_t (*stroke_rect)(void *context, const hk_display_t *session,
                               const hk_display_rect_t *rect, uint16_t color);
    hk_result_t (*text)(void *context, const hk_display_t *session,
                        const hk_display_rect_t *bounds, const char *utf8,
                        uint32_t size_bytes, uint16_t color);
    hk_result_t (*blit)(void *context, const hk_display_t *session,
                        const hk_display_rect_t *destination,
                        const hk_buffer_view_t *pixels, uint32_t pixel_format);
    hk_result_t (*mark_dirty)(void *context, const hk_display_t *session,
                              const hk_display_rect_t *rect);
    hk_result_t (*surface_acquire)(void *context, const hk_display_t *session,
                                   hk_display_surface_t *surface);
    hk_result_t (*present)(void *context, const hk_display_t *session,
                           hk_deadline_t deadline, const hk_cancel_t *cancel);
    hk_result_t (*abort)(void *context, const hk_display_t *session);
    hk_result_t (*stage_checkpoint)(void *context, const hk_display_t *session,
                                    uint16_t *commands, uint16_t *text_bytes);
    hk_result_t (*stage_restore)(void *context, const hk_display_t *session,
                                 uint16_t commands, uint16_t text_bytes);
    hk_result_t (*stage_keep_last_clear)(void *context,
                                         const hk_display_t *session);
    uint32_t reserved;
};

extern const hk_display_service_t hk_display_binding;

#endif
