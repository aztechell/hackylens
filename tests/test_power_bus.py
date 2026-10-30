import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from board_contract import load_board
from gen_board import write_board_config


class PowerBusTests(unittest.TestCase):
    def test_failure_snapshot_precedes_cleanup_and_i2c1_recovers_with_bounded_init(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="power-bus-") as directory:
            temp = Path(directory)
            write_board_config(load_board("sipeed-maix-cube"), temp / "board_config.h")
            includes = [temp, ROOT / "platforms/k210/hal",
                        ROOT / "_deps/kendryte-standalone-sdk/lib/drivers/include",
                        ROOT / "_deps/kendryte-standalone-sdk/lib/bsp/include"]
            executable = temp / ("power.exe" if os.name == "nt" else "power")
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            *(f"-I{path}" for path in includes),
                            str(ROOT / "tests/power_bus_harness.c"), "-o", str(executable)],
                           check=True, cwd=ROOT)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("POWER_BUS_OK", result.stdout)
