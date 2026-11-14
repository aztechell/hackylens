import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AprilTagDistanceTests(unittest.TestCase):
    def test_calibration_tilted_pose_and_gravity_projection(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-tag-range-") as temporary:
            binary = Path(temporary) / ("range.exe" if os.name == "nt" else "range")
            subprocess.run(
                [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(ROOT / "tests/apriltag_distance_harness.c"),
                 str(ROOT / "firmware/src/apps/apriltag/apriltag_distance.c"),
                 "-lm", "-o", str(binary)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(binary)], check=True, timeout=10)
