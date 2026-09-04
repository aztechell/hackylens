import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PongHostTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_fixed_step_physics_and_dirty_rendering(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-pong-") as temp:
            executable = Path(temp) / (
                "pong_host_test.exe" if os.name == "nt" else "pong_host_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'sdk' / 'include'}",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src'}",
                str(ROOT / "tests" / "pong_harness.c"),
                str(ROOT / "firmware" / "src" / "apps" / "pong" / "pong_controller.c"),
                str(ROOT / "firmware" / "src" / "apps" / "pong" / "pong_view.c"),
                str(ROOT / "firmware" / "src" / "app_runtime" / "surface.c"),
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
            "PONG_HOST_OK fixed_step=20ms dirty_regions=bounded presents=1",
            result.stdout,
        )

    def test_production_runtime_back_closes_and_input_moves(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-pong-runtime-") as temp:
            executable = Path(temp) / (
                "pong_runtime.exe" if os.name == "nt" else "pong_runtime"
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'sdk' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                    f"-I{ROOT / 'tests'}",
                    str(ROOT / "tests" / "pong_runtime_harness.c"),
                    str(ROOT / "firmware" / "src" / "app_runtime" / "runtime.c"),
                    str(ROOT / "firmware" / "src" / "app_runtime" / "surface.c"),
                    str(ROOT / "firmware" / "src" / "app_runtime" / "switch.c"),
                    str(ROOT / "firmware" / "src" / "capabilities" / "capability_core.c"),
                    str(ROOT / "firmware" / "src" / "capabilities" / "time.c"),
                    str(ROOT / "firmware" / "src" / "capabilities" / "input.c"),
                    str(ROOT / "firmware" / "src" / "capabilities" / "input_state.c"),
                    str(ROOT / "tests" / "time_normative_fake_backend.c"),
                    str(ROOT / "tests" / "input_normative_fake_backend.c"),
                    str(ROOT / "tests" / "capability_fake_display.c"),
                    str(ROOT / "tests" / "app_runtime_host_support.c"),
                    str(ROOT / "firmware" / "src" / "apps" / "pong" / "pong_app.c"),
                    str(ROOT / "firmware" / "src" / "apps" / "pong" / "pong_controller.c"),
                    str(ROOT / "firmware" / "src" / "apps" / "pong" / "pong_view.c"),
                    str(ROOT / "firmware" / "src" / "apps" / "buttons" / "buttons_app.c"),
                    str(ROOT / "firmware" / "src" / "apps" / "buttons" / "buttons_controller.c"),
                    str(ROOT / "firmware" / "src" / "apps" / "buttons" / "buttons_view.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
        self.assertIn("PONG_RUNTIME_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
