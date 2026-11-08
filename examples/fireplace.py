# Full-screen fireplace for HackyLens with display_rects().
import hackylens as hl

width, height = hl.display_size()
X = [i * width // 80 for i in range(81)]
Y = [i * height // 60 for i in range(61)]
data = bytearray(2560)
used = 0


def flush():
    global used
    if used:
        hl.display_rects(data, used)
        used = 0


def box(x, y, w, h, color):
    global used
    right = x + w
    bottom = y + h
    if x < 0:
        x = 0
    if y < 0:
        y = 0
    if right > 80:
        right = 80
    if bottom > 60:
        bottom = 60
    if x >= right or y >= bottom:
        return
    x = X[x]
    y = Y[y]
    w = X[right] - x
    h = Y[bottom] - y
    pos = used * 10
    data[pos] = x & 255
    data[pos + 1] = x >> 8
    data[pos + 2] = y & 255
    data[pos + 3] = y >> 8
    data[pos + 4] = w & 255
    data[pos + 5] = w >> 8
    data[pos + 6] = h & 255
    data[pos + 7] = h >> 8
    data[pos + 8] = color & 255
    data[pos + 9] = color >> 8
    used += 1


def rgb(r, g, b):
    return ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)


wave = (0, 2, 3, 4, 6, 7, 7, 8, 8, 8, 7, 7, 6, 4, 3, 2,
        0, -2, -3, -4, -6, -7, -7, -8, -8, -8, -7, -7, -6, -4, -3, -2)
tongues = ((19, 22, 9), (29, 29, 11), (40, 37, 13),
           (51, 31, 11), (61, 23, 9))
layers = ((100, 100, 0x7800, 0xC180), (88, 84, 0xBA00, 0xFA80),
          (68, 65, 0xFBE0, 0xFCC2), (46, 44, 0xFEE7, 0xFFAD))

# Back log, crossed front logs, bark and split ends, in scene coordinates.
wood = (
    (4, 53, 72, 7, 0x1802), (8, 54, 63, 3, 0x4984),
    (12, 52, 28, 2, 0x5A06), (14, 50, 20, 2, 0x4184),
    (19, 49, 18, 1, 0xB346), (32, 51, 19, 2, 0x9204),
    (39, 52, 21, 2, 0x6184), (49, 53, 19, 4, 0x3985),
    (55, 54, 15, 2, 0x6984), (64, 55, 6, 3, 0xA388),
    (66, 56, 3, 2, 0x6984), (65, 57, 4, 1, 0x3943),
    (11, 57, 47, 3, 0x3103), (16, 56, 47, 3, 0x3903),
    (23, 55, 40, 3, 0x5144), (31, 54, 29, 2, 0x69C4),
    (39, 53, 20, 1, 0xB2A3), (37, 55, 19, 1, 0xA2C5),
    (22, 57, 18, 1, 0x7307), (16, 58, 7, 2, 0x8B88),
    (18, 58, 3, 2, 0x5A45), (51, 55, 7, 1, 0xF489),
)
seed = (hl.ticks_ms() & 65535) or 0xC0DE


def noise():
    global seed
    seed ^= (seed << 7) & 65535
    seed ^= seed >> 9
    seed ^= (seed << 8) & 65535
    return seed


sparks = [[13 + noise() % 54, 10 + noise() % 43] for i in range(13)]


def backdrop(t):
    hl.display_clear(0x0801)
    pulse = wave[(t // 2) & 31] // 3
    for y in range(4, 60, 4):
        light = (y * y) // 92
        box(0, y, 80, 4, rgb(9 + light, 5 + light // 4, 9 + light // 9))
    for y in range(20, 60, 4):
        light = (y - 20) // 4
        edge = max(0, 13 - light * 2)
        box(edge, y, 80 - 2 * edge, 4,
            rgb(18 + light * 6 + pulse, 6 + light * 2, 13))
        box(edge + 11, y, 58 - 2 * edge, 4,
            rgb(23 + light * 9 + pulse, 6 + light * 3, 12))


def flames(t):
    for tall, wide, rim, body in layers:
        for i in range(5):
            x, peak, radius = tongues[i]
            phase = t + i * 9
            bend = wave[phase & 31]
            peak += bend // 2 + wave[(phase * 2 + 5) & 31] // 4
            span = peak * tall // 100
            for rise in range(0, span, 3):
                thickness = radius * wide * (span - rise) // (100 * span)
                ripple = wave[(phase + rise * 2) & 31]
                half = max(1, thickness + ripple // 4)
                tip = x + (ripple + bend) * (rise + 9) // 85
                band = min(3, span - rise)
                color = rim if rise * 5 >= span * 3 else body
                box(tip - half, 54 - rise - band, half * 2 + 1, band, color)


def embers(t):
    for item in wood:
        x, y, w, h, color = item
        box(x, y, w, h, color)
    for i in range(10):
        jitter = noise()
        box(4 + i * 7 + (jitter & 3), 58 + ((jitter >> 4) & 1),
            2 + (jitter & 3), 1, 0xFBE3 if jitter & 16 else 0xA1A3)
    for i in range(len(sparks)):
        spark = sparks[i]
        spark[1] -= 1 + ((t + i) & 1)
        spark[0] += wave[(t + i * 5) & 31] // 6
        if spark[1] < 5 or spark[0] < 4 or spark[0] > 74:
            spark[0] = 14 + noise() % 54
            spark[1] = 45 + noise() % 11
        color = 0xFF30 if spark[1] > 33 else 0xD300
        box(spark[0], spark[1], 1 + (i & 1), 1, color)


frame = 0
while not hl.button(hl.BUTTON_BACK):
    started = hl.ticks_ms()
    backdrop(frame)
    flames(frame)
    embers(frame)
    flush()
    hl.display_present()
    frame = (frame + 1) & 1023
    remaining = 66 - (hl.ticks_ms() - started)
    if remaining > 0:
        hl.sleep_ms(remaining)
