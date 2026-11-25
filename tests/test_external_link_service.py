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

import gen_board


class ExternalLinkServiceTests(unittest.TestCase):
    def test_native_service_uses_capability_and_restores_transport(self) -> None:
        for board_id in ("huskylens-sen0305", "sipeed-maix-cube"):
            with self.subTest(board=board_id):
                self._check_service(board_id)

    def _check_service(self, board_id: str) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        with tempfile.TemporaryDirectory(prefix="hackylens-external-service-") as temp:
            gen_board.write_board_config(gen_board.load_board(board_id), Path(temp) / "board_config.h")
            executable = Path(temp) / (
                "external_link_service.exe" if os.name == "nt"
                else "external_link_service"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{temp}",
                str(ROOT / "tests" / "external_link_service_harness.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "external_link_service.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "external_link_scan.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "external_link_protocol.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "vision_result_service.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        self.assertIn(
            "EXTERNAL_LINK_SERVICE_OK protocol=v1 reacquire=1 "
            "uart_async=1 target=1 vision_results=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
