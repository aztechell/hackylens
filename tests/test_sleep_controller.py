import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SleepControllerTests(unittest.TestCase):
    def test_monotonic_request_and_failure_safe_inactivity(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-sleep-") as temp:
            executable = Path(temp) / (
                "sleep_controller.exe" if os.name == "nt" else "sleep_controller"
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    f"-I{ROOT / 'sdk' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src'}",
                    str(ROOT / "tests" / "sleep_controller_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "controllers"
                        / "auto_sleep_controller.c"
                    ),
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

        self.assertIn(
            "SLEEP_CONTROLLER_OK monotonic_only=1 failure_safe=1 wrap_safe=1",
            result.stdout,
        )

    def test_production_runtime_wakes_on_input_and_cleans_up(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-sleep-runtime-") as temp:
            executable = Path(temp) / (
                "sleep_runtime.exe" if os.name == "nt" else "sleep_runtime"
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    '-DHACKYLENS_VERSION="host"',
                    f"-I{ROOT / 'sdk' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'apps' / 'sleep'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                    f"-I{ROOT / 'firmware' / 'assets'}",
                    f"-I{ROOT / 'tests'}",
                    str(ROOT / "tests" / "sleep_runtime_harness.c"),
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
                    str(ROOT / "firmware" / "src" / "apps" / "sleep" / "sleep_app.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "sleep"
                        / "sleep_controller.c"
                    ),
                    str(ROOT / "firmware" / "src" / "apps" / "sleep" / "sleep_view.c"),
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
        self.assertIn("SLEEP_RUNTIME_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
