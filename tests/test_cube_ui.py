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


class CubeUiTests(unittest.TestCase):
    def compile_and_run(self, name, sources, board_id="sipeed-maix-cube", args=()):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-cube-ui-") as directory:
            temp = Path(directory)
            write_board_config(load_board(board_id), temp / "board_config.h")
            executable = temp / (name + (".exe" if os.name == "nt" else ""))
            includes = ["firmware/include", "sdk/include", "firmware/src/core",
                        "firmware/src/ui", "firmware/src/config", "firmware/assets",
                        "firmware/src/capabilities", "platforms/k210/hal"]
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                f"-I{temp}", *(f"-I{ROOT / path}" for path in includes),
                *(str(ROOT / path) for path in sources), "-o", str(executable),
            ], check=True, cwd=ROOT)
            return subprocess.run([str(executable), *args], check=True, text=True,
                                  capture_output=True, timeout=30).stdout

    def test_center_is_normal_ok_and_short_power_is_back(self):
        output = self.compile_and_run("input", [
            "tests/cube_input_harness.c", "platforms/k210/capabilities/input_adapter.c",
            "firmware/src/capabilities/input_state.c",
        ])
        self.assertIn("CUBE_INPUT_OK", output)

    def test_native_layout_and_camera_coordinate_mapping(self):
        output = self.compile_and_run("layout", [
            "tests/cube_layout_harness.c", "firmware/src/ui/camera_view.c",
            "firmware/src/ui/image_fit.c", "firmware/src/core/hk_menu.c",
            "firmware/src/core/image_viewport.c",
            "firmware/src/apps/apriltag/apriltag_view.c",
            "firmware/src/apps/face_detect/face_detect_view.c",
            "firmware/src/apps/object_detect/object_detect_view.c",
            "firmware/src/apps/object_detect/object_detect_labels.c",
        ])
        self.assertIn("CUBE_LAYOUT_OK", output)

    def test_viewport_modes_are_independent_of_board_geometry(self):
        output = self.compile_and_run("viewport", [
            "tests/image_viewport_harness.c", "firmware/src/core/image_viewport.c",
            "firmware/src/ui/image_fit.c",
        ])
        self.assertIn("IMAGE_VIEWPORT_OK", output)

    def test_camera_restarts_apply_mounting_rotation_and_keep_capture_dimensions(self):
        for board_id, rotate in (("sipeed-maix-cube", "1"), ("huskylens-sen0305", "0")):
            with self.subTest(board=board_id):
                output = self.compile_and_run("orientation", [
                    "tests/camera_orientation_harness.c",
                    "firmware/src/services/camera_session.c",
                    "firmware/src/services/camera_session_state.c",
                    "firmware/src/services/camera_frame_access.c",
                    "firmware/src/core/hk_camera_sizes.c",
                ], board_id=board_id, args=(rotate,))
                self.assertIn("CAMERA_ORIENTATION_OK", output)


if __name__ == "__main__":
    unittest.main()
