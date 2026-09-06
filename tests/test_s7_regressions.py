from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class S7Regressions(unittest.TestCase):
    def run_harness(self, name, source):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "test.exe"
            subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-flto", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                '-DHACKYLENS_VERSION="host"', f"-I{ROOT / 'sdk/include'}", f"-I{ROOT / 'firmware/include'}",
                str(ROOT / 'tests' / name), str(ROOT / source), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    def test_gif_timing_decodes_pixels_and_preserves_pause(self):
        self.run_harness("s7_gif_harness.c", "firmware/src/apps/files/image_decode_gif.c")
    def test_resource_completion_never_polls_inactive_apps(self):
        self.run_harness("s7_cleanup_harness.c", "firmware/src/services/resource_cleanup.c")

    def test_short_press_during_gif_is_retained_for_normal_dispatch(self):
        self.run_harness("files_gif_input_harness.c", "firmware/src/capabilities/input_state.c")
