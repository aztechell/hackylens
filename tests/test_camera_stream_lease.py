from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CameraStreamLeaseTests(unittest.TestCase):
    def test_stale_lease_and_exhausted_ids(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="hackylens-camera-lease-") as temp:
            executable = Path(temp) / (
                "camera_stream_lease.exe" if os.name == "nt" else "camera_stream_lease"
            )
            subprocess.run(
                [
                    str(compiler), "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                    str(ROOT / "tests" / "camera_stream_lease_harness.c"),
                    str(ROOT / "firmware" / "src" / "core" / "shared_buffer.c"),
                    str(ROOT / "tests" / "hal_shared_buffer_fake.c"),
                    "-o", str(executable),
                ],
                check=True, cwd=ROOT,
            )
            result = subprocess.run(
                [str(executable)], cwd=ROOT, capture_output=True, text=True, timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("CAMERA_STREAM_LEASE_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
