from __future__ import annotations

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


class SettingsAutostartPersistenceTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler is required")
        return compiler

    def test_uint16_round_trip_and_v4_migration(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-settings-autostart-") as directory:
            temporary = Path(directory)
            (temporary / "hk_config.h").write_text(
                "#define HK_ENABLE_CAMERA_FEATURE 0\n", encoding="utf-8"
            )
            executable = temporary / (
                "settings_autostart.exe" if os.name == "nt" else "settings_autostart"
            )
            subprocess.run([
                self.compiler(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                f"-I{temporary}", f"-I{ROOT / 'firmware/src'}",
                f"-I{ROOT / 'firmware/src/services'}",
                f"-I{ROOT / 'firmware/src/storage'}",
                f"-I{ROOT / 'firmware/src/config'}",
                f"-I{gen_board.board_config_include_dir()}",
                str(ROOT / "tests/settings_autostart_persistence_harness.c"),
                str(ROOT / "firmware/src/storage/settings_store.c"),
                str(ROOT / "firmware/src/services/settings_payload_codec.c"),
                str(ROOT / "firmware/src/core/hk_binary.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT, text=True,
                capture_output=True, timeout=30,
            )
        self.assertIn(
            "SETTINGS_AUTOSTART_OK uint16=300 migrated_v4=10 record=124",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
