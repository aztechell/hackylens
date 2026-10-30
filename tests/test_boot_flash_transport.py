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


class BootFlashTransportTests(unittest.TestCase):
    def run_harness(self, name, sources, generated_board=False):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="flash-transport-") as directory:
            temp = Path(directory)
            if generated_board:
                write_board_config(load_board("sipeed-maix-cube"), temp / "board_config.h")
            executable = temp / (name + (".exe" if os.name == "nt" else ""))
            includes = [temp, ROOT / "tests/cube_spi_stubs", ROOT / "platforms/k210/hal",
                        ROOT / "firmware/src/drivers", ROOT / "firmware/src/internal"]
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                *(f"-I{path}" for path in includes),
                str(ROOT / f"tests/{name}_harness.c"),
                *(str(ROOT / path) for path in sources), "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout

    def test_pio_timeouts_restore_irq_and_chip_select_for_enabled_and_disabled_callers(self):
        output = self.run_harness("flash_pio", ["platforms/k210/hal/hal_spi.c",
                                              "tests/hal_shared_buffer_fake.c"])
        self.assertIn("FLASH_PIO_OK", output)

    def test_boot_flash_propagates_transport_errors_and_polls_busy_with_irqs_enabled(self):
        output = self.run_harness("boot_flash_transport", ["firmware/src/drivers/boot_flash.c"], True)
        self.assertIn("BOOT_FLASH_TRANSPORT_OK", output)
