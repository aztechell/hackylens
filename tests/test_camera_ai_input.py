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


class CameraAiInputTests(unittest.TestCase):
    def run_harness(self, harness, board):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory(prefix="camera-ai-") as directory:
            temp = Path(directory)
            write_board_config(load_board(board), temp / "board_config.h")
            executable = temp / ("camera-ai.exe" if os.name == "nt" else "camera-ai")
            includes = ["firmware/include", "firmware/src/core", "firmware/src/internal",
                        "firmware/src/services", "firmware/src/drivers", "platforms/k210/hal"]
            sources = [f"tests/{harness}_harness.c", "firmware/src/core/camera_ai_transform.c",
                       "firmware/src/core/image_viewport.c"]
            if harness == "camera_ai_input":
                sources.append("firmware/src/services/camera_ai_input.c")
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", f"-I{temp}",
                *(f"-I{ROOT / path}" for path in includes),
                *(str(ROOT / path) for path in sources), "-o", str(executable),
            ], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout

    def test_color_planes_mounting_rotation_letterbox_and_inverse_boxes(self):
        self.assertIn("CAMERA_AI_TRANSFORM_OK", self.run_harness("camera_ai_transform", "sipeed-maix-cube"))

    def test_planar_slot_stays_immutable_until_completion_for_both_capture_paths(self):
        for board in ("sipeed-maix-cube", "huskylens-sen0305"):
            with self.subTest(board=board):
                self.assertIn("CAMERA_AI_INPUT_OK", self.run_harness("camera_ai_input", board))
