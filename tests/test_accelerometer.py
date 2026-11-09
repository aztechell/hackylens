from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


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
