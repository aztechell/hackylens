import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import board_contract
import gen_board


class DisplayTransportTests(unittest.TestCase):
    def test_st7789_stream_lifecycle(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-lcd-stream-") as temp:
            executable = Path(temp) / (
                "lcd_stream_test.exe" if os.name == "nt" else "lcd_stream_test"
            )
            generated = Path(temp) / "board_config.h"
            gen_board.write_board_config(
                board_contract.load_board("huskylens-sen0305"), generated
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                f"-I{generated.parent}",
                str(ROOT / "tests" / "lcd_st7789_stream_harness.c"),
                str(ROOT / "firmware" / "src" / "drivers" / "lcd_st7789.c"),
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
            "LCD_ST7789_STREAM_OK slices=20 begin=1 end=1 cancel=no-late-write",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
