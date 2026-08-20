import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import board_contract
import gen_capability_inventory as generator


class InputCapabilityTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_debounce_events_cursors_and_overflow(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-input-") as temp:
            temporary = Path(temp)
            executable = temporary / (
                "input_capability.exe" if os.name == "nt" else "input_capability"
            )
            input_object = temporary / "input_state.o"
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
            ]
            subprocess.run([
                compiler, *common, "-c",
                str(ROOT / "firmware" / "src" / "capabilities" / "input_state.c"),
                "-o", str(input_object),
            ], check=True, cwd=ROOT)
            subprocess.run([
                compiler, *common,
                str(ROOT / "tests" / "input_capability_harness.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "input.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "input_state.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "capability_core.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(input_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in ("malloc", "calloc", "realloc", "free", "task", "queue"):
                    self.assertNotIn(forbidden, symbols)

        self.assertIn(
            "INPUT_CAPABILITY_OK events=12 capacity=8 dropped=11",
            result.stdout,
        )

    def test_k210_raw_sampler_is_gated_to_ten_milliseconds(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-k210-input-") as temp:
            executable = Path(temp) / (
                "k210_input_adapter.exe" if os.name == "nt" else "k210_input_adapter"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                str(ROOT / "tests" / "k210_input_adapter_harness.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" / "input_adapter.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "input_state.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )

        self.assertIn(
            "K210_INPUT_SAMPLING_OK reads=4 accepted_us=30000",
            result.stdout,
        )

    def test_composition_requires_input_and_cube_stays_absent(self) -> None:
        apps = set(generator.load_app_requirements())
        runtime_board = board_contract.load_board("huskylens-sen0305")
        cube = board_contract.load_board("sipeed-maix-cube")
        runtime = generator.compose(runtime_board, apps, set(), set(), set())
        conformance = generator.compose(cube, apps, set(), set(), set())

        self.assertEqual(
            [item.id for item in runtime.capabilities],
            ["hackylens.cap.time", "hackylens.cap.input",
             "hackylens.cap.display", "hackylens.cap.lights"],
        )
        self.assertEqual(
            [item.id for item in conformance.capabilities],
            ["hackylens.cap.time"],
        )
        disabled = generator.compose(
            runtime_board, apps, set(), set(), {"hackylens.cap.input"},
        )
        self.assertEqual(disabled.disabled_apps, frozenset(apps))
        with self.assertRaisesRegex(generator.CapabilityError, "required app"):
            generator.compose(
                runtime_board, apps, set(), {"pong"},
                {"hackylens.cap.input"},
            )

    def test_runtime_and_micropython_share_public_provider(self) -> None:
        runtime = (ROOT / "firmware" / "src" / "runtime" / "hk_main.c").read_text(
            encoding="utf-8"
        )
        service = (
            ROOT / "firmware" / "src" / "services" / "micropython_binding_service.c"
        ).read_text(encoding="utf-8")
        driver = (
            ROOT / "firmware" / "src" / "drivers" / "board_buttons.c"
        ).read_text(encoding="utf-8")

        self.assertIn("hk_input_next_event", runtime)
        self.assertIn("hk_input_get_state", runtime)
        self.assertIn("hk_input_get_state", service)
        self.assertNotIn("hk_input_poll", runtime + service)
        self.assertNotIn("buttons_poll", runtime + service + driver)
        self.assertNotIn("BUTTON_DEBOUNCE_POLLS", driver)


if __name__ == "__main__":
    unittest.main()
