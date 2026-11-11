from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from board_contract import load_board
from gen_board import write_board_config


class AccelerometerTests(unittest.TestCase):
    def test_msa301_configuration_signed_scale_and_recovery(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-accelerometer-") as directory:
            executable = Path(directory) / "accelerometer_test.exe"
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware/include'}",
                f"-I{ROOT / 'platforms/k210/hal'}",
                str(ROOT / "tests/accelerometer_harness.c"),
                str(ROOT / "firmware/src/capabilities/accelerometer.c"),
                str(ROOT / "platforms/k210/capabilities/accelerometer_adapter.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_full_trace_survives_real_k210_display_command_limit(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        includes = (
            "firmware/include", "sdk/include", "tests", "firmware/src/capabilities",
            "firmware/src/config", "firmware/src/core", "firmware/src/drivers",
            "firmware/src/ui", "firmware/assets", "platforms/k210/hal",
        )
        sources = (
            "tests/accelerometer_display_harness.c", "tests/display_normative_suite.c",
            "platforms/k210/capabilities/display_adapter.c",
            "firmware/src/capabilities/display.c", "firmware/src/services/frame_pool.c",
            "firmware/src/app_runtime/surface.c", "firmware/src/ui/app_panel.c",
            "firmware/src/ui/hk_font.c", "firmware/src/core/hk_string.c",
            "firmware/src/apps/accelerometer/accelerometer_app.c",
        )
        with tempfile.TemporaryDirectory(prefix="hackylens-accel-display-") as directory:
            temporary = Path(directory)
            write_board_config(load_board("sipeed-maix-cube"), temporary / "board_config.h")
            executable = temporary / "accelerometer_display.exe"
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-DK210_DISPLAY_ADAPTER_TESTING", f"-I{temporary}",
                *(f"-I{ROOT / path}" for path in includes),
                *(str(ROOT / path) for path in sources), "-lm", "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run([str(executable)], check=True, capture_output=True,
                                    text=True, timeout=20)
            self.assertIn("ACCELEROMETER_DISPLAY_OK", result.stdout)
