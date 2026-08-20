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
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
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
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                str(ROOT / "tests" / "hal_external_link_harness.c"),
                str(
                    ROOT
                    / "platforms"
                    / "k210"
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

    def test_k210_display_adapter_composition_cancel_and_cleanup(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-display-") as temp:
            executable = Path(temp) / (
                "k210_display_test.exe" if os.name == "nt" else "k210_display_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DK210_DISPLAY_ADAPTER_TESTING",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'firmware' / 'src' / 'ui'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                str(ROOT / "tests" / "k210_display_harness.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" /
                    "display_adapter.c"),
                str(ROOT / "firmware" / "src" / "ui" / "hk_font.c"),
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
            "K210_DISPLAY_ADAPTER_OK cases=8 slice=128 framebuffer=153600",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
