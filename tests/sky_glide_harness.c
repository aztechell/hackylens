#include <stdio.h>

#include "sky_glide_game.h"

int main(void)
{
    sky_game_t game;
    uint32_t digest = 0;
    sky_game_reset(&game);
    for(unsigned step = 0; step < SKY_LOGIC_STEPS; step++)
    {
        sky_game_step(&game, sky_replay_flap(step));
        if((step & 255U) == 255U)
            digest = sky_digest(digest, &game);
        if(step >= 1600)
            continue;
        printf("S %d %d %d %u %u %u %u\n", (int)game.y, (int)game.velocity,
               game.gate_x, game.score, game.gate_index, game.passed, game.phase);
        if(step % 13U != 0U)
            continue;
        for(unsigned width = 240; width <= 320; width += 80)
        {
            sky_rect_t rects[SKY_MAX_RECTS];
            size_t count = sky_game_scene(&game, (uint16_t)width, rects);
            printf("R %u", width);
            for(size_t i = 0; i < count; i++)
            {
                sky_rect_t r = rects[i];
                printf(" %u,%u,%u,%u,%u", r.x, r.y, r.width, r.height, r.color);
            }
            putchar('\n');
        }
    }
    printf("D %06x\n", (unsigned)digest);
    return 0;
}
