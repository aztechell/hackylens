import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PowerOffTests(unittest.TestCase):
    def test_confirmation_and_ordered_shutdown_failures(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-power-off-") as temp:
            temp = Path(temp)
            (temp / "hk_config.h").write_text("#define HK_ENABLE_APP_MICROPYTHON 1\n")
            executable = temp / ("power_off.exe" if os.name == "nt" else "power_off")
            subprocess.run([compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{temp}", f"-I{ROOT / 'firmware/include'}", f"-I{ROOT / 'sdk/include'}", f"-I{ROOT / 'platforms/k210/hal'}",
                str(ROOT / "tests/power_off_harness.c"),
                str(ROOT / "firmware/src/apps/power_off/power_off_app.c"),
                str(ROOT / "firmware/src/capabilities/power.c"),
                str(ROOT / "firmware/src/services/power_shutdown.c"),
                str(ROOT / "platforms/k210/capabilities/power_adapter.c"),
                "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True, timeout=30)
            self.assertIn("POWER_OFF_OK", result.stdout)
