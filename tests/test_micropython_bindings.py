import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MicroPythonBindingSafetyTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_rpc_cancel_uart_progress_and_cleanup_contract(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-mpy-bindings-") as temp:
            executable = Path(temp) / (
                "micropython_bindings_test.exe"
                if os.name == "nt"
                else "micropython_bindings_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DMICROPYTHON_BINDING_TESTING",
                f"-I{ROOT / 'tests'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                str(ROOT / "tests" / "micropython_bindings_harness.c"),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "services"
                    / "micropython_binding_service.c"
                ),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn("MICROPYTHON_BINDINGS_OK cases=8", result.stdout)

    def test_uart_fifo_capacity_boundaries(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-uart-hal-") as temp:
            executable = Path(temp) / (
                "hal_external_link_test.exe"
                if os.name == "nt"
                else "hal_external_link_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DHAL_EXTERNAL_LINK_TESTING",
                f"-I{ROOT / 'tests'}",
                f"-I{ROOT / 'firmware' / 'src' / 'hal'}",
                str(ROOT / "tests" / "hal_external_link_harness.c"),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "hal"
                    / "hal_external_link.c"
                ),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn(
            "HAL_EXTERNAL_LINK_OK boundaries=4 depth=8 idle=3", result.stdout
        )

    def test_lcd_overlay_composition_cancel_and_cleanup(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-lcd-overlay-") as temp:
            executable = Path(temp) / (
                "lcd_overlay_test.exe" if os.name == "nt" else "lcd_overlay_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DLCD_ST7789_TESTING",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'src' / 'board'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'firmware' / 'src' / 'hal'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                str(ROOT / "tests" / "lcd_overlay_harness.c"),
                str(ROOT / "firmware" / "src" / "drivers" / "lcd_st7789.c"),
                str(ROOT / "firmware" / "src" / "core" / "hk_string.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn("LCD_OVERLAY_OK cases=8", result.stdout)


if __name__ == "__main__":
    unittest.main()
