"""Confirm a person in the central zone using the installed VOC20 model."""

TARGET_CLASS = 14
MIN_CONFIDENCE = 500
STABLE_FRAMES = 3
GUIDE = 0x07ff
CANDIDATE = 0xfd20
CONFIRMED = 0x07e0

BENCH_FRAMES = 0
WARMUP = 4


class Gate:
    def __init__(self):
        self.hits = 0
        self.misses = 0
        self.occupied = False
        self.entries = 0

    def step(self, width, height, items):
        left, top = width // 4, height // 4
        right, bottom = width - left, height - top
        best = None
        score = MIN_CONFIDENCE - 1
        for kind, _, label, x0, y0, x1, y1, confidence in items:
            if kind != 1 or label != TARGET_CLASS or confidence <= score:
                continue
            if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
                continue
            if left <= (x0 + x1) // 2 < right and top <= (y0 + y1) // 2 < bottom:
                best = (x0, y0, x1 - x0, y1 - y0)
                score = confidence
        if best is None:
            self.hits = 0
            self.misses = min(STABLE_FRAMES, self.misses + 1)
            if self.misses == STABLE_FRAMES:
                self.occupied = False
            return (left, top, right - left, bottom - top), GUIDE

        self.misses = 0
        self.hits = min(STABLE_FRAMES, self.hits + 1)
        if self.hits == STABLE_FRAMES and not self.occupied:
            self.occupied = True
            self.entries = (self.entries + 1) & 0xffffffff
        return best, CONFIRMED if self.hits == STABLE_FRAMES else CANDIDATE


def snapshot_digest(digest, width, height, items):
    for value in (width, height, len(items)):
        digest = (digest * 33 + value) & 0xffffffff
    for item in items:
        for value in item:
            digest = (digest * 33 + value) & 0xffffffff
    return digest


def run():
    import hackylens as hl

    gate = Gate()
    bench = BENCH_FRAMES > WARMUP
    processed = presented = digest = 0
    detect_us = policy_us = present_us = 0
    window_start = window_end = 0

    hl.camera_open()
    try:
        begin = hl.ticks_us()
        hl.camera_model_open(hl.VISION_OBJECT_DETECT, "object20")
        load_us = hl.ticks_us() - begin
        last_ok = hl.button(hl.BUTTON_OK)
        while not hl.button(hl.BUTTON_BACK):
            ok = hl.button(hl.BUTTON_OK)
            if ok and not last_ok:
                gate = Gate()
            last_ok = ok
            frame = hl.camera_frame()
            if frame is None:
                hl.sleep_ms(5)
                continue
            lease, width, height, sequence = frame
            try:
                measured = bench and processed >= WARMUP
                if measured:
                    started = hl.ticks_us()
                    if processed == WARMUP:
                        window_start = started
                result = hl.camera_detect(lease)
                if measured:
                    completed = hl.ticks_us()
                    detect_us += completed - started
                if (result["frame_id"] != sequence or result["width"] != width or
                        result["height"] != height or result["source"] != hl.VISION_OBJECT_DETECT):
                    raise RuntimeError("detector returned a different frame")
                if measured:
                    started = hl.ticks_us()
                box, color = gate.step(width, height, result["items"])
                if measured:
                    policy_us += hl.ticks_us() - started
                    digest = snapshot_digest(digest, width, height, result["items"])
                    started = hl.ticks_us()
                hl.camera_present(lease, box, color)
                if measured:
                    window_end = hl.ticks_us()
                    present_us += window_end - started
                    presented += 1
            finally:
                hl.camera_release(lease)
            if bench:
                processed += 1
                if processed == BENCH_FRAMES:
                    break
    finally:
        hl.camera_close()
    if bench:
        print("OG RESULT", processed - WARMUP, presented, gate.entries, digest,
              load_us, detect_us, policy_us, present_us, window_end - window_start)


if __name__ == "__main__":
    run()
