#include "sky_glide_game.h"

enum
{
    UNIT = 256, CEILING = 26, FLOOR = 218,
    GLIDER_X = 54, GLIDER_W = 18, GLIDER_H = 10,
    GATE_W = 22, GATE_SPACING = 132, GAP = 82
};

static const uint8_t gaps[] = {108, 118, 132, 114, 96, 106, 121, 135};
static const uint8_t digits[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                 0x6d, 0x7d, 0x07, 0x7f, 0x6f};

void sky_game_reset(sky_game_t *game)
{
    *game = (sky_game_t){.y = 112 * UNIT, .gate_x = SKY_SIZE};
}

bool sky_game_step(sky_game_t *game, bool flap)
{
    if(game->phase != SKY_PLAYING)
    {
        if(!flap)
            return false;
        sky_game_reset(game);
        game->phase = SKY_PLAYING;
    }
    if(flap)
        game->velocity = -1024;
    game->velocity += 80;
    if(game->velocity > 1280)
        game->velocity = 1280;
    game->y += game->velocity;
    game->gate_x -= 2;
    if(game->y < CEILING * UNIT || game->y + GLIDER_H * UNIT > FLOOR * UNIT ||
       (game->gate_x < GLIDER_X + GLIDER_W && game->gate_x + GATE_W > GLIDER_X &&
        (game->y < (gaps[game->gate_index] - GAP / 2) * UNIT ||
         game->y + GLIDER_H * UNIT > (gaps[game->gate_index] + GAP / 2) * UNIT)))
    {
        game->phase = SKY_CRASHED;
        return true;
    }
    if(!game->passed && game->gate_x + GATE_W <= GLIDER_X)
    {
        game->passed = 1;
        game->score++;
    }
    if(game->gate_x <= -GATE_W)
    {
        game->gate_x += GATE_SPACING;
        game->gate_index = (game->gate_index + 1U) & 7U;
        game->passed = 0;
    }
    return true;
}

bool sky_replay_flap(uint32_t step)
{
    /* Repeated flights with regular early impulses and a silent interval
       that reaches a collision. Include the restart in each cycle. */
    const uint32_t beat = step & 255U;
    return beat < 140U && beat % 25U == 0U;
}

uint32_t sky_digest(uint32_t digest, const sky_game_t *game)
{
    const int32_t values[] = {game->y, game->velocity, game->gate_x, game->score,
                              game->gate_index, game->passed, game->phase};
    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        digest = (digest * 33U + (uint32_t)values[i]) & 0xffffffU;
    return digest;
}

static void rect(sky_rect_t **next, int offset, int x, int y, int w, int h,
                 uint16_t color)
{
    int right = x + w;
    int bottom = y + h;
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
    if(right > SKY_SIZE)
        right = SKY_SIZE;
    if(bottom > SKY_SIZE)
        bottom = SKY_SIZE;
    if(right > x && bottom > y)
    {
        **next = (sky_rect_t){(uint16_t)(x + offset), (uint16_t)y,
                              (uint16_t)(right - x), (uint16_t)(bottom - y), color};
        ++*next;
    }
}

static void gate(sky_rect_t **next, int offset, int x, int center)
{
    const int top = center - GAP / 2;
    const int bottom = center + GAP / 2;
    rect(next, offset, x, CEILING, GATE_W, top - CEILING, 0x1453);
    rect(next, offset, x + 3, top - 4, GATE_W - 6, 4, 0x4fff);
    rect(next, offset, x, bottom, GATE_W, FLOOR - bottom, 0x1453);
    rect(next, offset, x + 3, bottom, GATE_W - 6, 4, 0x4fff);
}

static void digit(sky_rect_t **next, int offset, int x, uint8_t value)
{
    static const uint8_t bars[7][4] = {
        {2, 0, 6, 2}, {8, 2, 2, 5}, {8, 9, 2, 5}, {2, 14, 6, 2},
        {0, 9, 2, 5}, {0, 2, 2, 5}, {2, 7, 6, 2}
    };
    for(uint8_t i = 0; i < 7; i++)
        if(digits[value] & (1U << i))
            rect(next, offset, x + bars[i][0], 5 + bars[i][1],
                 bars[i][2], bars[i][3], 0xf7ff);
}

size_t sky_game_scene(const sky_game_t *game, uint16_t width,
                      sky_rect_t rects[SKY_MAX_RECTS])
{
    sky_rect_t *next = rects;
    const int offset = (width - SKY_SIZE) / 2;
    const int y = game->y / UNIT;
    const uint16_t indicator = game->phase == SKY_CRASHED ? 0xf9cb :
                               game->phase == SKY_READY ? 0xff32 : 0x4fff;
    rect(&next, offset, 0, 0, SKY_SIZE, SKY_SIZE, 0x112b);
    rect(&next, offset, 0, 26, SKY_SIZE, 192, 0x19cf);
    rect(&next, offset, 18, 57, 40, 2, 0x2a94);
    rect(&next, offset, 145, 72, 58, 2, 0x2a94);
    rect(&next, offset, 110, 157, 66, 2, 0x2a94);
    rect(&next, offset, 0, 216, SKY_SIZE, 2, 0x2a94);
    gate(&next, offset, game->gate_x, gaps[game->gate_index]);
    gate(&next, offset, game->gate_x + GATE_SPACING,
         gaps[(game->gate_index + 1U) & 7U]);
    rect(&next, offset, GLIDER_X, y, 4, 3, 0xffff);
    rect(&next, offset, GLIDER_X + 4, y + 2, 6, 3, 0xffff);
    rect(&next, offset, GLIDER_X + 2, y + 4, 16, 2, 0xffff);
    rect(&next, offset, GLIDER_X + 5, y + 6, 9, 2, 0x9f7f);
    rect(&next, offset, GLIDER_X + 8, y + 8, 4, 2, 0x9f7f);
    rect(&next, offset, 14, 228, 44, 3, indicator);
    if(game->phase != SKY_PLAYING)
    {
        rect(&next, offset, 93, 165, 54, 22, 0x112b);
        /* Pixel lettering for OK. */
        rect(&next, offset, 107, 170, 8, 2, indicator);
        rect(&next, offset, 107, 180, 8, 2, indicator);
        rect(&next, offset, 107, 172, 2, 8, indicator);
        rect(&next, offset, 113, 172, 2, 8, indicator);
        rect(&next, offset, 121, 170, 2, 12, indicator);
        rect(&next, offset, 125, 174, 2, 4, indicator);
        rect(&next, offset, 127, 170, 2, 4, indicator);
        rect(&next, offset, 127, 178, 2, 4, indicator);
    }
    digit(&next, offset, 103, (game->score / 100) % 10);
    digit(&next, offset, 115, (game->score / 10) % 10);
    digit(&next, offset, 127, game->score % 10);
    return (size_t)(next - rects);
}
