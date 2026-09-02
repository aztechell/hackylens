from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class Phase2QualificationTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("host C compiler is required")
        return compiler

    def test_registry_validation_host_p99(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-phase2-latency-") as temp:
            executable = Path(temp) / (
                "phase2_latency.exe" if os.name == "nt" else "phase2_latency"
            )
            subprocess.run([
                self.compiler(), "-std=c11", "-O2", "-Wall", "-Wextra",
                "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'tests'}",
                str(ROOT / "tests" / "phase2_registry_latency_harness.c"),
                str(ROOT / "tests" / "capability_fake_provider.c"),
                str(ROOT / "firmware" / "src" / "capabilities" /
                    "capability_core.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT, text=True,
                capture_output=True, timeout=30,
            )
        match = re.search(r"host_p99_ns=(\d+) limit_us=100", result.stdout)
        self.assertIsNotNone(match, result.stdout)
        self.assertLessEqual(int(match.group(1)), 100_000)


if __name__ == "__main__":
    unittest.main()
