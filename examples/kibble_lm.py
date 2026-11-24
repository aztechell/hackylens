"""Generate and scroll a KibbleLM v0.2 story on either K210 board."""

PROMPTS = ("Once upon a time", "A little dog", "One sunny day", "In a small town")
LOADING, READY, GENERATING, DONE, ERROR = range(5)
GLYPH_WIDTH, LINE_HEIGHT = 14, 24
BODY_TOP, MARGIN = 32, 8
OK_HOLD_MS = 700
SCROLL_DELAY_MS, SCROLL_REPEAT_MS = 500, 180


def wrap_story(text, columns):
    lines = []
    while text:
        end = min(columns, len(text))
        if end < len(text) and text[end] != " ":
            space = text.rfind(" ", 0, end)
            if space > 0:
                end = space
        lines.append(text[:end])
        text = text[end:].lstrip(" ")
    return lines


def finish_at_word_boundary(text, prompt_length):
    end = len(text)
    while end > prompt_length:
        char = text[end - 1]
        if not (char.isalpha() or char.isdigit() or char in "'-"):
            break
        end -= 1
    return text[:prompt_length] + text[prompt_length:end].rstrip(" ")


class StoryApp:
    def __init__(self, board):
        self.board = board
        width, height = board.display_size()
        self.columns = (width - 2 * MARGIN) // GLYPH_WIDTH
        self.status_y = height - LINE_HEIGHT - 4
        self.rows = (self.status_y - BODY_TOP) // LINE_HEIGHT
        self.phase = LOADING
        self.error = ""
        self.prompt_index = 0
        self.previous = board.buttons()
        self.ok_due = None
        self.ok_can_start = False
        self.scroll_button = 0
        self.scroll_due = 0
        self.reset_output(PROMPTS[0])

    def reset_output(self, prompt):
        self.text = prompt
        self.prompt_length = len(prompt)
        self.tokens = 0
        self.first_line = None
        self.last_first_line = 0
        self.scroll_button = 0

    def open_step(self):
        if not self.board.kibble_open_step():
            return False
        self.phase = READY
        return True

    def start(self, prompt, seed):
        self.board.kibble_start(prompt, seed)
        self.reset_output(prompt)
        self.phase = GENERATING

    def poll(self):
        result = self.board.kibble_poll()
        if result is None:
            return None
        piece, end, _, _ = result
        if not end:
            self.tokens += 1
            self.text += piece
        if end or self.tokens == self.board.KIBBLE_MAX_TOKENS:
            if not end:
                self.text = finish_at_word_boundary(self.text, self.prompt_length)
            self.phase = DONE
        return result

    def clear(self):
        self.reset_output(PROMPTS[self.prompt_index])
        if self.phase == GENERATING:
            self.board.kibble_close()
            self.phase = LOADING
        elif self.phase == DONE:
            self.phase = READY

    def move(self, direction):
        if self.phase in (GENERATING, DONE):
            line = self.last_first_line if self.first_line is None else self.first_line
            next_line = max(0, min(self.last_first_line, line + direction))
            if line == next_line:
                return False
            self.first_line = None if next_line == self.last_first_line else next_line
        else:
            self.prompt_index = (self.prompt_index + direction) % len(PROMPTS)
            self.reset_output(PROMPTS[self.prompt_index])
        return True

    def input_step(self, buttons, now):
        board = self.board
        pressed = buttons & ~self.previous
        released = self.previous & ~buttons
        self.previous = buttons
        if released & self.scroll_button:
            self.scroll_button = 0
        if pressed & board.BUTTON_OK and self.phase != LOADING:
            self.ok_due = now + OK_HOLD_MS
            self.ok_can_start = self.phase != GENERATING
        if self.ok_due is not None:
            if now >= self.ok_due:
                self.ok_due = None
                self.clear()
                return True
            if released & board.BUTTON_OK:
                self.ok_due = None
                if self.ok_can_start and self.phase in (READY, DONE):
                    self.start(PROMPTS[self.prompt_index], board.ticks_us() & 0xffffffff)
                    return True
                if self.phase == ERROR:
                    self.phase = LOADING
                    return True

        directions = pressed & (board.BUTTON_LEFT | board.BUTTON_RIGHT)
        if directions:
            forward = bool(directions & board.BUTTON_RIGHT)
            if self.phase in (GENERATING, DONE):
                self.scroll_button = board.BUTTON_RIGHT if forward else board.BUTTON_LEFT
                self.scroll_due = now + SCROLL_DELAY_MS
            return self.move(1 if forward else -1)
        if self.scroll_button and now >= self.scroll_due:
            self.scroll_due = now + SCROLL_REPEAT_MS
            return self.move(1 if self.scroll_button == board.BUTTON_RIGHT else -1)
        return False

    def render(self):
        board = self.board
        status = ("Loading model", "Choose a prompt", "Generating", "Done", "OK to retry")[self.phase]
        body = self.error if self.phase == ERROR else self.text
        lines = wrap_story(body, self.columns)
        self.last_first_line = max(0, len(lines) - self.rows)
        first = self.last_first_line if self.first_line is None else min(
            self.first_line, self.last_first_line)
        if self.first_line is not None:
            self.first_line = first
        if self.last_first_line and self.phase in (GENERATING, DONE):
            status = "{} {}-{}/{}".format(
                "Gen" if self.phase == GENERATING else "Done",
                first + 1, first + self.rows, len(lines))
        board.display_clear(0)
        board.display_text(MARGIN, 4, "KibbleLM PY", 0x07ff)
        for row in range(first, min(first + self.rows, len(lines))):
            board.display_text(MARGIN, BODY_TOP + (row - first) * LINE_HEIGHT, lines[row])
        board.display_text(MARGIN, self.status_y, status, 0x7bef)
        board.display_present()


def run():
    import hackylens as hl

    if not hasattr(hl, "kibble_open_step"):
        raise RuntimeError("KibbleLM firmware required")
    app = StoryApp(hl)
    app.render()
    try:
        while True:
            buttons = hl.buttons()
            if buttons & hl.BUTTON_BACK:
                break
            try:
                redraw = app.input_step(buttons, hl.ticks_ms())
                if app.phase == LOADING:
                    redraw = app.open_step() or redraw
                elif app.phase == GENERATING:
                    redraw = (app.poll() is not None) or redraw
            except (OSError, ValueError, RuntimeError) as error:
                if app.phase not in (LOADING, ERROR):
                    hl.kibble_close()
                app.error = str(error)
                app.first_line = None
                app.phase = ERROR
                redraw = True
            if redraw:
                app.render()
            hl.sleep_ms(5 if app.phase in (LOADING, GENERATING) else 20)
    finally:
        if app.phase != ERROR:
            hl.kibble_close()


if __name__ == "__main__":
    run()
