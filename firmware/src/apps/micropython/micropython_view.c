#include "micropython_view.h"

#include <stdio.h>

#include "../../config/display_config.h"
#include "../../drivers/hk_lcd.h"
#include "../../ui/hk_ui.h"

static const char *runtime_label(micropython_runtime_state_t state)
{
    switch(state)
    {
    case MICROPYTHON_RUNTIME_STARTING: return "STARTING";
    case MICROPYTHON_RUNTIME_RUNNING: return "RUNNING";
    case MICROPYTHON_RUNTIME_STOPPING: return "STOPPING";
    case MICROPYTHON_RUNTIME_FINISHED: return "FINISHED";
    case MICROPYTHON_RUNTIME_ERROR: return "ERROR";
    default: return "STOPPED";
    }
}

static const char *filesystem_label(userfs_state_t state)
{
    switch(state)
    {
    case USERFS_STATE_MOUNTED: return "MOUNTED";
    case USERFS_STATE_UNFORMATTED: return "UNFORMATTED";
    case USERFS_STATE_UNSUPPORTED_FLASH: return "UNSUPPORTED";
    case USERFS_STATE_CORRUPT: return "CORRUPT";
    case USERFS_STATE_IO_ERROR: return "I/O ERROR";
    default: return "OFFLINE";
    }
}

void micropython_view_render(
    const micropython_runtime_status_t *runtime,
    const userfs_status_t *filesystem,
    const char *startup,
    const char logs[MICROPYTHON_LOG_LINES][MICROPYTHON_LOG_COLUMNS + 1U])
{
    char line[48];
    uint16_t state_color;

    menu_draw_chrome("MICRO-PYTHON");
    lcd_fill_rect(0U, 30U, LCD_W, LCD_H - 30U, COLOR_BLACK);
    state_color = runtime->state == MICROPYTHON_RUNTIME_ERROR ? COLOR_WHITE :
                  COLOR_TERM_GREEN;
    snprintf(line, sizeof(line), "VM %-8s RUN %u",
             runtime_label(runtime->state), (unsigned)runtime->run_id);
    lcd_draw_text_at(6U, 34U, line, state_color, COLOR_BLACK);
    snprintf(line, sizeof(line), "FS %-11s %lu/%luK",
             filesystem_label(filesystem->state),
             (unsigned long)(filesystem->used_bytes / 1024U),
             (unsigned long)(filesystem->total_bytes / 1024U));
    lcd_draw_text_at(6U, 52U, line, COLOR_TERM_GREEN, COLOR_BLACK);
    snprintf(line, sizeof(line), "STARTUP %s", startup && startup[0] ? startup : "OFF");
    lcd_draw_text_at(6U, 70U, line, COLOR_TERM_GREEN, COLOR_BLACK);

    lcd_draw_text_at(6U, 89U, "--- LOG ---", COLOR_WHITE, COLOR_BLACK);
    for(uint8_t row = 0U; row < MICROPYTHON_LOG_LINES; row++)
        lcd_draw_text_at(6U, (uint16_t)(107U + row * 16U), logs[row],
                         COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_at(6U, 222U, "OK RUN/STOP  BACK MENU", COLOR_WHITE, COLOR_BLACK);
}

void micropython_view_draw_icon(uint16_t x, uint16_t y,
                                uint16_t color, uint16_t background)
{
    (void)background;
    lcd_draw_rect(x + 9U, y + 10U, 42U, 40U, 2U, color);
    lcd_draw_text_at(x + 16U, y + 22U, "PY", color, COLOR_BLACK);
    lcd_fill_rect(x + 12U, y + 13U, 7U, 3U, color);
    lcd_fill_rect(x + 41U, y + 44U, 7U, 3U, color);
}
