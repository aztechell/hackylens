from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MicroPythonRuntimeTests(unittest.TestCase):
    def test_busy_and_invalid_run_preserve_active_worker(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="hackylens-micropython-runtime-") as temp:
            temp_path = Path(temp)
            (temp_path / "hk_config.h").write_text(
                "#define HK_MICROPYTHON_WDT_FAULT_INJECTION 0\n", encoding="utf-8"
            )
            executable = temp_path / (
                "micropython_runtime.exe" if os.name == "nt" else "micropython_runtime"
            )
            subprocess.run(
                [
                    str(compiler), "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'sdk' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                    f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                    f"-I{ROOT / 'tests' / 'fixtures' / 'micropython_runtime_stub'}",
                    f"-I{temp_path}",
                    str(ROOT / "tests" / "micropython_runtime_harness.c"),
                    str(ROOT / "firmware" / "src" / "services" / "micropython_runtime.c"),
                    "-o", str(executable),
                ],
                check=True, cwd=ROOT,
            )
            result = subprocess.run(
                [str(executable)], cwd=ROOT, capture_output=True,
                text=True, timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("MICROPYTHON_RUNTIME_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
