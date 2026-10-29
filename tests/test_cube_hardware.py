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


class CubeHardwareTests(unittest.TestCase):
    def test_octal_phases_preserve_clock_and_frame_format(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-cube-spi-") as temp:
            executable = Path(temp) / ("cube_spi.exe" if os.name == "nt" else "cube_spi")
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'tests/cube_spi_stubs'}",
                f"-I{ROOT / 'platforms/k210/hal'}",
                str(ROOT / "tests/cube_spi_harness.c"),
                str(ROOT / "platforms/k210/hal/hal_spi.c"),
                str(ROOT / "tests/hal_shared_buffer_fake.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run([str(executable)], check=True, cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertIn("CUBE_SPI_OK", result.stdout)

    def test_power_short_press_latch_and_bus_failure_recovery(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-cube-pek-") as temp:
            executable = Path(temp) / ("axp173.exe" if os.name == "nt" else "axp173")
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware/src/drivers'}",
                f"-I{ROOT / 'platforms/k210/hal'}",
                str(ROOT / "tests/axp173_harness.c"),
                str(ROOT / "firmware/src/drivers/axp173.c"), "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run([str(executable)], check=True, capture_output=True,
                                    text=True, timeout=30)
            self.assertIn("AXP173_OK", result.stdout)

    def test_sensor_transport_and_lights_use_cube_electrical_profile(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-cube-hardware-") as temp:
            directory = Path(temp)
            write_board_config(load_board("sipeed-maix-cube"), directory / "board_config.h")
            executable = directory / ("cube_hardware.exe" if os.name == "nt" else "cube_hardware")
            sources = [
                ROOT / "tests/cube_hardware_harness.c",
                ROOT / "firmware/src/drivers/camera_sensor.c",
                ROOT / "firmware/src/drivers/ov7740_sensor.c",
                ROOT / "firmware/src/drivers/lights.c",
                ROOT / "firmware/src/drivers/lcd_st7789.c",
                ROOT / "firmware/src/core/hk_binary.c",
            ]
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{directory}",
                f"-I{ROOT / 'firmware/include'}",
                f"-I{ROOT / 'firmware/src/drivers'}",
                f"-I{ROOT / 'firmware/src/internal'}",
                f"-I{ROOT / 'platforms/k210/hal'}",
                *(str(source) for source in sources), "-lm", "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run([str(executable)], check=True, cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertIn("CUBE_HARDWARE_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
