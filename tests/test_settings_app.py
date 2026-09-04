import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SettingsAppTests(unittest.TestCase):
    def test_navigation_change_save_and_reopen(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-settings-") as temp:
            executable = Path(temp) / (
                "settings_controller.exe" if os.name == "nt" else "settings_controller"
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    '-DHACKYLENS_VERSION="host"',
                    f"-I{ROOT / 'sdk' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'apps' / 'settings'}",
                    str(ROOT / "tests" / "settings_controller_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "settings"
                        / "settings_controller.c"
                    ),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "controllers"
                        / "settings_menu_controller.c"
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
            "SETTINGS_CONTROLLER_OK nav=1 change=1 save=1 reopen=1",
            result.stdout,
        )

    def test_production_runtime_delivers_back_and_stop_cleanup(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-settings-runtime-") as temp:
            executable = Path(temp) / (
                "settings_runtime.exe" if os.name == "nt" else "settings_runtime"
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
                    f"-I{ROOT / 'firmware' / 'src' / 'apps' / 'settings'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                    f"-I{ROOT / 'firmware' / 'assets'}",
                    f"-I{ROOT / 'tests'}",
                    str(ROOT / "tests" / "settings_runtime_harness.c"),
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
                    str(ROOT / "firmware" / "src" / "apps" / "settings" / "settings_app.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "settings"
                        / "settings_controller.c"
                    ),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "controllers"
                        / "settings_menu_controller.c"
                    ),
                    str(ROOT / "firmware" / "src" / "apps" / "settings" / "settings_view.c"),
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
        self.assertIn("SETTINGS_RUNTIME_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
