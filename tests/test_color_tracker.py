import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "firmware" / "src" / "core"


class ColorTrackerTests(unittest.TestCase):
    def test_rgb565_components(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-color-tracker-") as temp:
            name = "color_tracker_test.exe" if os.name == "nt" else "color_tracker_test"
            executable = Path(temp) / name
            subprocess.run(
                [
                    compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    f"-I{CORE}", str(ROOT / "tests" / "color_tracker_harness.c"),
                    str(CORE / "color_tracker.c"), "-o", str(executable),
                ],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=10)
