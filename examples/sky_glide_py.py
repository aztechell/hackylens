"""Sky Glide for the SEN0305 and Maix Cube. OK flaps, BACK exits."""

BENCH_MODE = "play"
FRAME_PATH = "auto"

SIZE = 240
STEP_US = 20000
FRAME_US = 40000
BACKGROUND = 0x0865
MAX_RECTS = 64
LOGIC_STEPS = 32768
READY, PLAYING, CRASHED = 0, 1, 2
GAPS = (108, 118, 132, 114, 96, 106, 121, 135)
DIGITS = (0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f)
BARS = ((2, 0, 6, 2), (8, 2, 2, 5), (8, 9, 2, 5), (2, 14, 6, 2),
        (0, 9, 2, 5), (0, 2, 2, 5), (2, 7, 6, 2))


class Game:
    def __init__(self):
        self.reset()

    def reset(self):
        self.y = 112 * 256
        self.velocity = 0
        self.gate_x = SIZE
        self.score = 0
        self.gate_index = 0
        self.passed = 0
        self.phase = READY

    def step(self, flap):
        if self.phase != PLAYING:
            if not flap:
                return False
            self.reset()
            self.phase = PLAYING
        if flap:
            self.velocity = -1024
        self.velocity = min(self.velocity + 80, 1280)
        self.y += self.velocity
        self.gate_x -= 2
        gap = GAPS[self.gate_index]
        if (self.y < 26 * 256 or self.y + 10 * 256 > 218 * 256 or
                (self.gate_x < 72 and self.gate_x + 22 > 54 and
                 (self.y < (gap - 41) * 256 or self.y + 10 * 256 > (gap + 41) * 256))):
            self.phase = CRASHED
            return True
        if not self.passed and self.gate_x + 22 <= 54:
            self.passed = 1
            self.score = (self.score + 1) & 0xffff
        if self.gate_x <= -22:
            self.gate_x += 132
            self.gate_index = (self.gate_index + 1) & 7
            self.passed = 0
        return True

    def digest(self, digest):
        for value in (self.y, self.velocity, self.gate_x, self.score,
                      self.gate_index, self.passed, self.phase):
            digest = (digest * 33 + value) & 0xffffff
        return digest


def replay_flap(step):
    beat = step & 255
    return beat < 140 and beat % 25 == 0


def replay_to(game, step, target):
    while step < target:
        game.step(replay_flap(step))
        step += 1
    return step


class Frame:
    def __init__(self, width):
        self.offset = (width - SIZE) // 2
        self.data = bytearray(MAX_RECTS * 10)
        self.count = 0

    def rect(self, x, y, width, height, color):
        right = min(x + width, SIZE)
        bottom = min(y + height, SIZE)
        x = max(x, 0)
        y = max(y, 0)
        if right <= x or bottom <= y:
            return
        width, height = right - x, bottom - y
        x += self.offset
        pos = self.count * 10
        data = self.data
        data[pos] = x & 255
        data[pos + 1] = x >> 8
        data[pos + 2] = y & 255
        data[pos + 3] = y >> 8
        data[pos + 4] = width & 255
        data[pos + 5] = width >> 8
        data[pos + 6] = height & 255
        data[pos + 7] = height >> 8
        data[pos + 8] = color & 255
        data[pos + 9] = color >> 8
        self.count += 1

    def gate(self, x, center):
        top, bottom = center - 41, center + 41
        self.rect(x, 26, 22, top - 26, 0x1453)
        self.rect(x + 3, top - 4, 16, 4, 0x4fff)
        self.rect(x, bottom, 22, 218 - bottom, 0x1453)
        self.rect(x + 3, bottom, 16, 4, 0x4fff)

    def digit(self, x, value):
        for index in range(7):
            if DIGITS[value] & (1 << index):
                dx, dy, w, h = BARS[index]
                self.rect(x + dx, 5 + dy, w, h, 0xf7ff)

    def scene(self, game):
        self.count = 0
        y = game.y // 256
        indicator = (0xff32, 0x4fff, 0xf9cb)[game.phase]
        self.rect(0, 0, SIZE, SIZE, 0x112b)
        self.rect(0, 26, SIZE, 192, 0x19cf)
        self.rect(18, 57, 40, 2, 0x2a94)
        self.rect(145, 72, 58, 2, 0x2a94)
        self.rect(110, 157, 66, 2, 0x2a94)
        self.rect(0, 216, SIZE, 2, 0x2a94)
        self.gate(game.gate_x, GAPS[game.gate_index])
        self.gate(game.gate_x + 132, GAPS[(game.gate_index + 1) & 7])
        self.rect(54, y, 4, 3, 0xffff)
        self.rect(58, y + 2, 6, 3, 0xffff)
        self.rect(56, y + 4, 16, 2, 0xffff)
        self.rect(59, y + 6, 9, 2, 0x9f7f)
        self.rect(62, y + 8, 4, 2, 0x9f7f)
        self.rect(14, 228, 44, 3, indicator)
        if game.phase != PLAYING:
            self.rect(93, 165, 54, 22, 0x112b)
            self.rect(107, 170, 8, 2, indicator)
            self.rect(107, 180, 8, 2, indicator)
            self.rect(107, 172, 2, 8, indicator)
            self.rect(113, 172, 2, 8, indicator)
            self.rect(121, 170, 2, 12, indicator)
            self.rect(125, 174, 2, 4, indicator)
            self.rect(127, 170, 2, 4, indicator)
            self.rect(127, 178, 2, 4, indicator)
        self.digit(103, (game.score // 100) % 10)
        self.digit(115, (game.score // 10) % 10)
        self.digit(127, game.score % 10)


def choose_present(hl):
    if FRAME_PATH != "legacy":
        combined = getattr(hl, "display_frame", None)
        if combined is not None:
            return combined
        if FRAME_PATH == "combined":
            raise RuntimeError("display_frame unavailable")

    def legacy(background, data, count):
        hl.display_clear(background)
        hl.display_rects(data, count)
        hl.display_present()

    return legacy


def play(hl, clock, game, draw):
    ok_down = bool(hl.buttons() & hl.BUTTON_OK)
    flap = dirty = False
    draw()
    next_step = next_frame = clock()
    while True:
        buttons = hl.buttons()
        if buttons & hl.BUTTON_BACK:
            return
        down = bool(buttons & hl.BUTTON_OK)
        flap = flap or (down and not ok_down)
        ok_down = down
        now = clock()
        next_step = max(next_step, now - 4 * STEP_US)
        while now >= next_step:
            dirty = game.step(flap) or dirty
            flap = False
            next_step += STEP_US
        if dirty and now >= next_frame:
            next_frame += ((now - next_frame) // FRAME_US + 1) * FRAME_US
            draw()
            dirty = False
        hl.sleep_ms(5)


def benchmark(hl, clock, game, draw):
    mode = BENCH_MODE
    if mode == "logic":
        step = digest = elapsed = 0
        while step < LOGIC_STEPS:
            start = clock()
            step = replay_to(game, step, step + 256)
            elapsed += clock() - start
            digest = game.digest(digest)
        print("SG LOGIC", step, elapsed, digest)
    elif mode == "paced":
        start = clock()
        next_slot = step = 0
        while True:
            slot = (clock() - start) // FRAME_US
            if slot >= 800:
                break
            if slot < next_slot:
                hl.sleep_ms(1)
                continue
            next_slot = slot + 1
            step = replay_to(game, step, next_slot * 2)
            draw()
        print("SG PACED", start + 2000000, start + 32000000, 750)
    elif mode == "burst":
        step = 0
        for frame in range(350):
            if frame == 50:
                start = clock()
            step = replay_to(game, step, step + 2)
            draw()
        print("SG BURST", start, clock(), 300)
    elif mode == "input":
        game.phase = PLAYING
        draw()
        presses = []
        ok_down = bool(hl.buttons() & hl.BUTTON_OK)
        print("SG INPUT READY")
        while len(presses) < 20:
            buttons = hl.buttons()
            down = bool(buttons & hl.BUTTON_OK)
            if down and not ok_down:
                presses.append(clock())
                game.y = (112 if len(presses) % 2 == 0 else 84) * 256
                draw()
            ok_down = down
            hl.sleep_ms(5)
        for index in range(20):
            print("SG PRESS", index, presses[index])
        print("SG INPUT DONE")
    elif mode == "memory":
        import gc
        gc.collect()
        initial = peak = gc.mem_alloc()
        total = initial + gc.mem_free()
        step = 0
        for index in range(300):
            step = replay_to(game, step, step + 2)
            draw()
            peak = max(peak, gc.mem_alloc())
        gc.collect()
        print("SG MEMORY", total, initial, peak, gc.mem_alloc())
    else:
        raise ValueError("unknown Sky Glide benchmark")


def run():
    import hackylens as hl

    game = Game()
    clock = getattr(hl, "ticks_us", None)
    if clock is None:
        def clock():
            return hl.ticks_ms() * 1000
    if BENCH_MODE == "logic":
        benchmark(hl, clock, game, None)
        return
    width, height = hl.display_size()
    if width < SIZE or height != SIZE:
        raise RuntimeError("unsupported Sky Glide display")
    frame = Frame(width)
    present = choose_present(hl)

    def draw():
        frame.scene(game)
        present(BACKGROUND, frame.data, frame.count)

    if BENCH_MODE == "play":
        play(hl, clock, game, draw)
    else:
        benchmark(hl, clock, game, draw)


if __name__ == "__main__":
    run()
