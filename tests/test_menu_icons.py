from __future__ import annotations

import hashlib
import json
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
import make_menu_icons


class MenuIconTests(unittest.TestCase):
    def test_embedded_header_matches_approved_masks(self):
        self.assertEqual(make_menu_icons.HEADER.read_text(), make_menu_icons.generated_header())

    def test_actual_callbacks_match_approved_masks_in_both_states_and_boards(self):
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        manifest = json.loads((make_menu_icons.SOURCES / "manifest.json").read_text())
        masks = {}
        for app, checksum in manifest["sha256"].items():
            data = (make_menu_icons.SOURCES / f"{app}.1bpp").read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), checksum)
            masks[app] = data
        sources = [
            "tests/menu_icons_harness.c", "firmware/src/ui/ui_menu.c",
            "firmware/src/apps/camera/camera_view.c",
            "firmware/src/apps/face_detect/face_detect_view.c",
            "firmware/src/apps/apriltag/apriltag_view.c",
            "firmware/src/apps/object_detect/object_detect_view.c",
            "firmware/src/apps/micropython/micropython_view.c",
            "firmware/src/ui/camera_view.c", "firmware/src/ui/image_fit.c",
            "firmware/src/ui/hk_font.c", "firmware/src/core/image_viewport.c",
            "firmware/src/core/hk_string.c",
            "firmware/src/apps/object_detect/object_detect_labels.c",
        ]
        includes = ["firmware/include", "sdk/include", "firmware/src/core",
                    "firmware/src/config", "firmware/src/ui", "firmware/assets"]
        for board in ("sipeed-maix-cube", "huskylens-sen0305"):
            with self.subTest(board=board), tempfile.TemporaryDirectory(prefix="menu-icons-") as directory:
                temporary = Path(directory)
                descriptor = load_board(board)
                write_board_config(descriptor, temporary / "board_config.h")
                executable = temporary / ("menu-icons.exe" if os.name == "nt" else "menu-icons")
                compiled = subprocess.run([
                    compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    f"-I{temporary}", *(f"-I{ROOT / path}" for path in includes),
                    *(str(ROOT / path) for path in sources), "-o", str(executable),
                ], cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                result = subprocess.run([str(executable), str(temporary)], check=True,
                                        capture_output=True, text=True, timeout=30)
                self.assertIn("MENU_ICONS_OK", result.stdout)
                width = 240 if board == "sipeed-maix-cube" else 320
                stride = width * 2 + 16
                columns = 3 if width == 240 else 4
                for index, (app, mask) in enumerate(masks.items()):
                    slot = index % (columns * 3)
                    tx, ty = 12 + slot % columns * 78, 36 + slot // columns * 64
                    for selected in (0, 1):
                        with self.subTest(board=board, app=app, selected=selected):
                            actual = (temporary / f"icon-{index}-{selected}.rgb565").read_bytes()
                            self.assertEqual(len(actual), stride * 240)
                            expected = bytearray(b"\x5a" * (stride * 240))
                            for y in range(60):
                                for x in range(60):
                                    color = 0x07e0 if selected else 0
                                    if not selected and (x < 2 or x >= 58 or y < 2 or y >= 58):
                                        color = 0x07e0
                                    if selected and 4 <= x < 56 and 4 <= y < 56 and (x < 6 or x >= 54 or y < 6 or y >= 54):
                                        color = 0
                                    if 6 <= x < 54 and 6 <= y < 54:
                                        gx, gy = x - 6, y - 6
                                        bit = bool(mask[gy * 6 + gx // 8] & (0x80 >> (gx & 7)))
                                        color = 0x07e0 if bit != bool(selected) else 0
                                    at = (ty + y) * stride + (tx + x) * 2
                                    expected[at:at + 2] = color.to_bytes(2, "big")
                            self.assertEqual(actual, expected, "callback pixels or surrounding frame differ from approval")


if __name__ == "__main__":
    unittest.main()
