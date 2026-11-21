#ifndef SKY_GLIDE_GAME_H
#define SKY_GLIDE_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SKY_SIZE 240
#define SKY_STEP_US 20000U
#define SKY_FRAME_US 40000U
#define SKY_BACKGROUND 0x0865U
#define SKY_MAX_RECTS 64
#define SKY_LOGIC_STEPS 32768U

enum { SKY_READY, SKY_PLAYING, SKY_CRASHED };

typedef struct
{
    int32_t y;
    int32_t velocity;
    int16_t gate_x;
    uint16_t score;
    uint8_t gate_index;
    uint8_t passed;
    uint8_t phase;
} sky_game_t;

typedef struct
{
    uint16_t x, y, width, height, color;
} sky_rect_t;

void sky_game_reset(sky_game_t *game);
bool sky_game_step(sky_game_t *game, bool flap);
bool sky_replay_flap(uint32_t step);
uint32_t sky_digest(uint32_t digest, const sky_game_t *game);
size_t sky_game_scene(const sky_game_t *game, uint16_t width,
                      sky_rect_t rects[SKY_MAX_RECTS]);

#endif
