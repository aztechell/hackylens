import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "sky_glide_py", ROOT / "examples" / "sky_glide_py.py"
)
sky = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sky)


class SkyGlideTests(unittest.TestCase):
    def test_replay_and_both_display_sizes_match_native(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler not installed")
        native = ROOT / "firmware" / "src" / "apps" / "sky_glide"
        with tempfile.TemporaryDirectory(prefix="sky-glide-") as temp:
            program = Path(temp) / "sky_glide.exe"
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                "-I", str(native), str(ROOT / "tests" / "sky_glide_harness.c"),
                str(native / "sky_glide_game.c"), "-o", str(program),
            ], check=True)
            lines = iter(subprocess.check_output([program], text=True).splitlines())
        game = sky.Game()
        widths = (240, 320)
        frames = {width: sky.Frame(width) for width in widths}
        digest = 0
        scored = crashed = restarted = False
        for step in range(sky.LOGIC_STEPS):
            old_phase = game.phase
            game.step(sky.replay_flap(step))
            scored |= game.score > 0
            crashed |= game.phase == sky.CRASHED
            restarted |= old_phase == sky.CRASHED and game.phase == sky.PLAYING
            if (step & 255) == 255:
                digest = game.digest(digest)
            if step >= 1600:
                continue
            state = (game.y, game.velocity, game.gate_x, game.score,
                     game.gate_index, game.passed, game.phase)
            self.assertEqual(next(lines), "S " + " ".join(map(str, state)), step)
            if step % 13:
                continue
            for width in widths:
                frame = frames[width]
                frame.scene(game)
                coords = struct.iter_unpack("<5H", memoryview(frame.data)[:frame.count * 10])
                expected = "R " + str(width) + " " + " ".join(
                    ",".join(map(str, rect)) for rect in coords
                )
                self.assertEqual(next(lines), expected, (step, width))
        self.assertEqual(next(lines), f"D {digest:06x}")
        self.assertEqual(list(lines), [])
        self.assertTrue(scored and crashed and restarted)

    def test_default_frame_path_uses_legacy_on_0_9_0(self):
        called = []
        hl = SimpleNamespace(
            display_clear=lambda color: called.append(("clear", color)),
            display_rects=lambda data, count: called.append(("rects", data, count)),
            display_present=lambda: called.append(("present",)),
        )
        frame = bytearray(10)
        sky.choose_present(hl)(12, frame, 1)
        self.assertEqual(called, [("clear", 12), ("rects", frame, 1), ("present",)])
        hl.display_frame = lambda *args: None
        self.assertIs(sky.choose_present(hl), hl.display_frame)

    def test_play_consumes_one_flap_per_press_and_ignores_held_launcher_ok(self):
        samples = iter([2, 2, 0, 2, 2, 0, 2, 8])
        ticks = iter(range(100_000, 300_000, sky.STEP_US))
        calls = []
        hl = SimpleNamespace(
            buttons=lambda: next(samples), BUTTON_OK=2, BUTTON_BACK=8,
            sleep_ms=lambda _: None,
        )
        game = SimpleNamespace(step=lambda flap: calls.append(flap) or True)
        sky.play(hl, lambda: next(ticks), game, lambda: None)
        self.assertEqual([i for i, flap in enumerate(calls) if flap], [3, 6])
