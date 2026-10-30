import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QrMemoryTests(unittest.TestCase):
    def test_decoder_returns_all_heap_on_close_and_reopens_after_failure(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory(prefix="qr-memory-") as directory:
            temp = Path(directory)
            executable = temp / ("qr-memory.exe" if os.name == "nt" else "qr-memory")
            includes = ["firmware/third_party/quirc"]
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                *(f"-I{ROOT / path}" for path in includes),
                str(ROOT / "tests/qr_memory_harness.c"),
                str(ROOT / "firmware/src/apps/qr_camera/qr_luma.c"),
                "-o", str(executable),
            ], check=True)
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("QR_MEMORY_OK", result.stdout)
