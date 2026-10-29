from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SharedBufferTests(unittest.TestCase):
    def test_k210_boundaries_and_host_identity(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="hackylens-shared-buffer-") as temp:
            for k210 in (False, True):
                executable = Path(temp) / (
                    ("k210" if k210 else "identity") + (".exe" if os.name == "nt" else "")
                )
                subprocess.run(
                    [
                        str(compiler), "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                        f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                        *(["-DSHARED_BUFFER_K210_TEST"] if k210 else []),
                        str(ROOT / "tests" / "shared_buffer_harness.c"),
                        str(ROOT / "firmware" / "src" / "core" / "shared_buffer.c"),
                        str(
                            ROOT / (
                                "platforms/k210/hal/hal_shared_buffer.c"
                                if k210 else "tests/hal_shared_buffer_fake.c"
                            )
                        ),
                        "-o", str(executable),
                    ],
                    check=True, cwd=ROOT,
                )
                result = subprocess.run(
                    [str(executable)], cwd=ROOT, capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("SHARED_BUFFER_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
