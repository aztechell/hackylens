import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FilesControllerTests(unittest.TestCase):
    def test_ok_opens_with_least_privilege_and_failure_safe_timing(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-files-") as temp:
            executable = Path(temp) / (
                "files_controller.exe" if os.name == "nt" else "files_controller"
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
                    str(ROOT / "tests" / "files_controller_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "files"
                        / "files_controller.c"
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
            "FILES_CONTROLLER_OK open=1 monotonic=1 failure_safe=1",
            result.stdout,
        )

    def test_production_runtime_delivers_back_and_stop_cleanup(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-files-runtime-") as temp:
            executable = Path(temp) / (
                "files_runtime.exe" if os.name == "nt" else "files_runtime"
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
                    f"-I{ROOT / 'firmware' / 'src' / 'apps' / 'files'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                    f"-I{ROOT / 'tests'}",
                    str(ROOT / "tests" / "files_runtime_harness.c"),
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
                    str(ROOT / "firmware" / "src" / "apps" / "files" / "files_app.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "files"
                        / "files_controller.c"
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
        self.assertIn("FILES_RUNTIME_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
