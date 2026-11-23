"""Plot and record the HackyLens UART logger's eight-byte samples."""

BENCH_MODE = "play"
HISTORY = 96
QUEUE = 64
CSV_HEADER = b"seq,value\n"
GRID = ((20, 35, 200, 1, 0x528a), (20, 185, 200, 1, 0x528a),
        (20, 35, 1, 150, 0x528a), (219, 35, 1, 150, 0x528a),
        (21, 110, 198, 1, 0x3186))
WARMUP = 100
SAMPLES = 1600


def packet_crc(data, start):
    crc = 65535
    for i in range(start, start + 4):
        crc ^= data[i] << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 65535
    return crc


def put_word(data, offset, value):
    data[offset] = value & 255
    data[offset + 1] = (value >> 8) & 255


def encode_packet(data, seq, value):
    data[0] = 0xa5
    data[1] = 0x5a
    put_word(data, 2, seq)
    put_word(data, 4, value)
    put_word(data, 6, packet_crc(data, 2))


class Stream:
    def __init__(self):
        self.packet = bytearray(8)
        self.history = [0] * HISTORY
        self.used = self.next = self.count = 0
        self.accepted = self.gaps = self.crc_errors = self.stale = 0
        self.seq = self.value = 0
        self.digest = 2166136261

    def resynchronize(self):
        packet = self.packet
        for i in range(1, 7):
            if packet[i] == 0xa5 and packet[i + 1] == 0x5a:
                self.used = 8 - i
                for j in range(self.used):
                    packet[j] = packet[i + j]
                return
        self.used = 1 if packet[7] == 0xa5 else 0
        if self.used:
            packet[0] = 0xa5

    def feed(self, byte):
        if self.used == 0 and byte != 0xa5:
            return False
        if self.used == 1 and byte != 0x5a:
            self.used = 1 if byte == 0xa5 else 0
            return False
        packet = self.packet
        packet[self.used] = byte
        self.used += 1
        if self.used != 8:
            return False
        if packet_crc(packet, 2) != (packet[6] | (packet[7] << 8)):
            self.crc_errors += 1
            self.resynchronize()
            return False
        self.used = 0
        seq = packet[2] | (packet[3] << 8)
        if self.accepted:
            advance = (seq - self.seq) & 65535
            if advance == 0 or advance >= 32768:
                self.stale += 1
                return False
            self.gaps += advance - 1
        value = packet[4] | (packet[5] << 8)
        self.seq = seq
        self.value = value - 65536 if value >= 32768 else value
        self.accepted += 1
        for i in range(2, 6):
            self.digest = ((self.digest ^ packet[i]) * 16777619) & 0xffffffff
        self.history[self.next] = self.value
        self.next = (self.next + 1) % HISTORY
        if self.count < HISTORY:
            self.count += 1
        return True

    def point(self, index):
        value = self.history[(self.next + HISTORY - self.count + index) % HISTORY]
        value = min(1000, max(-1000, value))
        x = 24 + (HISTORY - self.count + index) * 2
        y = 38 + (1000 - value) * 144 // 2000
        return x, y, 2, 2, 0x07ff


def csv_row(seq, value):
    return str(seq) + "," + str(value) + "\n"


def file_name(clock):
    number = str((clock // 1000) % 1000000)
    return "TL" + "0" * (6 - len(number)) + number + ".CSV"


class Recorder:
    def __init__(self, hl):
        self.hl = hl
        self.state = "IDLE"
        self.name = ""
        self.seqs = [0] * QUEUE
        self.values = [0] * QUEUE
        self.read = self.count = self.offset = 0
        self.pending = b""
        self.header = False
        self.stop_requested = False

    def start(self, name):
        self.name = name
        self.read = self.count = self.offset = 0
        self.pending = b""
        self.header = True
        self.stop_requested = False
        self.state = "OPENING"

    def live(self):
        return self.state in ("OPENING", "RECORD", "SAVING")

    def stop(self):
        self.stop_requested = True
        if self.state == "RECORD":
            self.state = "SAVING"

    def fail(self):
        try:
            self.hl.sd_cancel()
        except (OSError, RuntimeError):
            pass
        self.state = "SD ERROR"

    def accept(self, seq, value):
        if self.stop_requested or self.state not in ("OPENING", "RECORD"):
            return
        if self.count == QUEUE:
            self.fail()
            return
        index = (self.read + self.count) % QUEUE
        self.seqs[index] = seq
        self.values[index] = value
        self.count += 1

    def next_block(self):
        block = "seq,value\n" if self.header else ""
        self.header = False
        for _ in range(min(16, self.count)):
            block += csv_row(self.seqs[self.read], self.values[self.read])
            self.read = (self.read + 1) % QUEUE
            self.count -= 1
        self.pending = bytes(block, "ascii")
        self.offset = 0

    def step(self):
        hl = self.hl
        try:
            if self.state == "OPENING":
                if hl.sd_begin(self.name):
                    self.state = "SAVING" if self.stop_requested else "RECORD"
                return
            if self.state not in ("RECORD", "SAVING"):
                return
            if self.offset == len(self.pending) and (self.count >= 16 or self.stop_requested):
                self.next_block()
            if self.offset < len(self.pending):
                tail = self.pending
                if self.offset:
                    tail = bytearray(len(self.pending) - self.offset)
                    for i in range(len(tail)):
                        tail[i] = self.pending[self.offset + i]
                self.offset += hl.sd_append(tail)
            elif self.stop_requested and hl.sd_finish():
                self.state = "SAVED"
        except (OSError, RuntimeError, ValueError):
            self.fail()


class Graph:
    def __init__(self, hl):
        width, height = hl.display_size()
        self.left = (width - 240) // 2
        self.top = (height - 240) // 2
        self.data = bytearray((HISTORY + len(GRID)) * 10)

    def rectangle(self, offset, rect):
        x, y, width, height, color = rect
        put_word(self.data, offset, self.left + x)
        put_word(self.data, offset + 2, self.top + y)
        put_word(self.data, offset + 4, width)
        put_word(self.data, offset + 6, height)
        put_word(self.data, offset + 8, color)

    def stage(self, hl, stream, state):
        index = 0
        for rect in GRID:
            self.rectangle(index * 10, rect)
            index += 1
        for point in range(stream.count):
            self.rectangle(index * 10, stream.point(point))
            index += 1
        hl.display_clear(0)
        hl.display_rects(self.data, index)
        hl.display_text(self.left + 20, self.top + 7, "UART LOGGER")
        headline = str(stream.value) + " LOST " + str(stream.gaps) if stream.accepted else "WAITING FOR UART"
        hl.display_text(self.left + 20, self.top + 188, headline)
        hl.display_text(self.left + 20, self.top + 213, state)


def parse_benchmark(hl):
    # A compact encoded block is reused so timed parsing fits the VM arena.
    sample = bytearray(8)
    data = bytearray(4096 * 8)
    for index in range(4096):
        encode_packet(sample, index, (index % 201 - 100) * 10)
        for i in range(8):
            data[index * 8 + i] = sample[i]
    stream = Stream()
    start = hl.ticks_us()
    for byte in data:
        stream.feed(byte)
    print("UL PARSE", stream.accepted, stream.digest, hl.ticks_us() - start)


def run():
    import hackylens as hl
    if BENCH_MODE == "parse":
        parse_benchmark(hl)
        return

    stream = Stream()
    recorder = Recorder(hl)
    graph = Graph(hl)
    bench = BENCH_MODE != "play"
    measuring = False
    ready = not bench
    done = False
    measured = uart_errors = first_sample = last_sample = 0
    render_due = hl.ticks_us()
    last_ok = hl.button(hl.BUTTON_OK)
    marks = bytearray(320 * 10) if bench else None
    origin = render_due
    count = 0

    hl.uart_init(115200)
    if BENCH_MODE == "record":
        recorder.start(file_name(render_due))
    try:
        while not hl.button(hl.BUTTON_BACK):
            now = hl.ticks_us()
            if not bench:
                ok = hl.button(hl.BUTTON_OK)
                if ok and not last_ok:
                    if recorder.live():
                        recorder.stop()
                    else:
                        recorder.start(file_name(now))
                last_ok = ok

            for _ in range(8):
                try:
                    data = hl.uart_read(32)
                except OSError:
                    uart_errors += 1
                    stream.used = 0
                    break
                for byte in data:
                    if stream.feed(byte):
                        last_sample = hl.ticks_us()
                        if not first_sample:
                            first_sample = last_sample
                        if not bench or (ready and WARMUP <= stream.seq < SAMPLES):
                            measured += 1
                            measuring = True
                            recorder.accept(stream.seq, stream.value)
                if len(data) < 32:
                    break

            if bench and first_sample and (stream.seq == SAMPLES - 1 or now - first_sample >= 33500000):
                done = True
                if recorder.live():
                    recorder.stop()
            storage_start = hl.ticks_us()
            recorder.step()

            if bench and not ready and recorder.state != "OPENING":
                if BENCH_MODE == "record" and recorder.state != "RECORD":
                    raise RuntimeError("cannot start CSV")
                ready = True
                print("UL READY", origin)
            now = hl.ticks_us()
            if bench and done:
                if not recorder.live():
                    break
            elif now >= render_due:
                render_due = now + 100000
                graph.stage(hl, stream, recorder.state)
                if bench and measuring and count < 320:
                    begin = hl.ticks_us() - origin
                    sample = last_sample - origin
                    offset = count * 10
                    put_word(marks, offset, begin)
                    put_word(marks, offset + 2, begin >> 16)
                    put_word(marks, offset + 4, sample)
                    put_word(marks, offset + 6, sample >> 16)
                    put_word(marks, offset + 8, stream.seq)
                    count += 1
                hl.display_present()
            if hl.ticks_us() - storage_start < 5000:
                hl.sleep_ms(5)
    finally:
        if recorder.live():
            hl.sd_cancel()

    if bench:
        print("UL RESULT", stream.accepted, measured, stream.gaps, stream.crc_errors,
              stream.stale, uart_errors, stream.digest, recorder.state, recorder.name or "-")
        for i in range(count):
            at = i * 10
            begin = marks[at] | (marks[at + 1] << 8) | (marks[at + 2] << 16) | (marks[at + 3] << 24)
            sample = marks[at + 4] | (marks[at + 5] << 8) | (marks[at + 6] << 16) | (marks[at + 7] << 24)
            seq = marks[at + 8] | (marks[at + 9] << 8)
            print("UL M", i, begin, sample, seq)
            if i % 8 == 7:
                hl.sleep_ms(350)
        print("UL END", count)


if __name__ == "__main__":
    run()
