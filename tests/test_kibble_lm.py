from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest


SPEC = spec_from_file_location(
    "kibble_lm", Path(__file__).resolve().parents[1] / "examples/kibble_lm.py"
)
kibble = module_from_spec(SPEC)
SPEC.loader.exec_module(kibble)


class Board:
    BUTTON_LEFT, BUTTON_OK, BUTTON_RIGHT = 1, 2, 4
    KIBBLE_MAX_TOKENS = 3

    def __init__(self, width):
        self.width = width
        self.started = []
        self.closed = 0
        self.drawn = []
        self.generated = iter(())

    def display_size(self):
        return self.width, 240

    def buttons(self):
        return self.BUTTON_OK

    def ticks_us(self):
        return 0x1000000d2

    def kibble_open_step(self):
        return True

    def kibble_start(self, prompt, seed):
        self.started.append((prompt, seed))

    def kibble_poll(self):
        return next(self.generated)

    def kibble_close(self):
        self.closed += 1

    def display_clear(self, color):
        self.drawn.clear()

    def display_text(self, x, y, text, color=0xffff):
        self.drawn.append((x, y, text))

    def display_present(self):
        pass


class KibbleLMTests(unittest.TestCase):
    def test_ok_release_starts_once_and_hold_clears(self):
        board = Board(240)
        app = kibble.StoryApp(board)
        app.open_step()
        app.input_step(0, 0)
        self.assertEqual(board.started, [])
        app.input_step(board.BUTTON_RIGHT, 10)
        app.input_step(board.BUTTON_OK, 20)
        app.input_step(board.BUTTON_OK, 200)
        self.assertEqual(board.started, [])
        app.input_step(0, 250)
        self.assertEqual(board.started, [("A little dog", 210)])
        app.input_step(board.BUTTON_OK, 300)
        app.input_step(board.BUTTON_OK, 1000)
        app.input_step(0, 1050)
        self.assertEqual(board.closed, 1)
        self.assertEqual(app.phase, kibble.LOADING)
        self.assertEqual(app.text, "A little dog")
        self.assertEqual(len(board.started), 1)

    def test_wrap_scroll_and_cut_at_token_limit_on_both_displays(self):
        for width in (240, 320):
            with self.subTest(width=width):
                board = Board(width)
                app = kibble.StoryApp(board)
                app.open_step()
                prompt = kibble.PROMPTS[0]
                app.start(prompt, 210)
                app.text += " the red fox went home" * 20
                app.render()
                last_first_line = app.last_first_line
                self.assertGreater(last_first_line, 3)
                app.input_step(board.BUTTON_LEFT, 10)
                self.assertEqual(app.first_line, last_first_line - 1)
                app.input_step(board.BUTTON_LEFT, 510)
                app.input_step(board.BUTTON_LEFT, 690)
                self.assertEqual(app.first_line, last_first_line - 3)
                app.render()
                self.assertTrue(all(x + len(text) * kibble.GLYPH_WIDTH <= width
                                    and y + kibble.LINE_HEIGHT <= 240
                                    for x, y, text in board.drawn))
                app.start(prompt, 210)
                board.generated = iter(((" a", False, 1, 2), (" dog", False, 1, 2),
                                        (" bark", False, 1, 2)))
                for _ in range(board.KIBBLE_MAX_TOKENS):
                    app.poll()
                self.assertEqual(app.phase, kibble.DONE)
                self.assertEqual(app.text, prompt + " a dog")
