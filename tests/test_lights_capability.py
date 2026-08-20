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
import gen_capability_inventory as generator


class LightsCapabilityTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_masks_deadlines_cleanup_and_bounded_object(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-lights-") as temp:
            temporary = Path(temp)
            executable = temporary / (
                "lights_capability.exe" if os.name == "nt" else "lights_capability"
            )
            lights_object = temporary / "lights.o"
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
            ]
            subprocess.run([
                compiler, *common, "-c",
                str(ROOT / "firmware" / "src" / "capabilities" / "lights.c"),
                "-o", str(lights_object),
            ], check=True, cwd=ROOT)
            subprocess.run([
                compiler, *common,
                str(ROOT / "tests" / "lights_capability_harness.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "lights.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "capability_core.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(lights_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in ("malloc", "calloc", "realloc", "free", "task", "queue"):
                    self.assertNotIn(forbidden, symbols)

        self.assertIn(
            "LIGHTS_CAPABILITY_OK writes=5 safe_off_mask=0x7 level_max=1000",
            result.stdout,
        )

    def test_k210_adapter_converts_only_after_cancel_and_deadline_checks(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-k210-lights-") as temp:
            executable = Path(temp) / (
                "k210_lights.exe" if os.name == "nt" else "k210_lights"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                str(ROOT / "tests" / "k210_lights_adapter_harness.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" / "lights_adapter.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )

        self.assertIn(
            "K210_LIGHTS_OK writes=4 illum_percent=51 rgb=100/50/0",
            result.stdout,
        )

    def test_settings_reacquire_replays_latest_values(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-settings-lights-") as temp:
            executable = Path(temp) / (
                "settings_lights.exe" if os.name == "nt" else "settings_lights"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                str(ROOT / "tests" / "settings_lights_harness.c"),
                str(ROOT / "firmware" / "src" / "services" / "settings_lights_apply.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )

        self.assertIn(
            "SETTINGS_LIGHTS_OK reacquired=2 latest=700/110/220/330",
            result.stdout,
        )

    def test_composition_and_all_consumers_share_the_provider(self) -> None:
        apps = set(generator.load_app_requirements())
        runtime_board = board_contract.load_board("huskylens-sen0305")
        cube = board_contract.load_board("sipeed-maix-cube")
        runtime = generator.compose(runtime_board, apps, set(), set(), set())
        conformance = generator.compose(cube, apps, set(), set(), set())

        self.assertEqual(
            [item.id for item in runtime.capabilities],
            ["hackylens.cap.time", "hackylens.cap.input",
             "hackylens.cap.display", "hackylens.cap.lights"],
        )
        self.assertEqual(
            [item.id for item in conformance.capabilities],
            ["hackylens.cap.time"],
        )
        disabled = generator.compose(
            runtime_board, apps, set(), set(), {"hackylens.cap.lights"},
        )
        for app in (
            "camera", "qr-camera", "face-detect", "apriltag",
            "object-detect", "settings", "sleep", "micropython",
        ):
            self.assertIn(app, disabled.disabled_apps)
        with self.assertRaisesRegex(generator.CapabilityError, "required app"):
            generator.compose(
                runtime_board, apps, set(), {"settings"},
                {"hackylens.cap.lights"},
            )

        settings = (
            ROOT / "firmware" / "src" / "services" / "settings_lights_apply.c"
        ).read_text(encoding="utf-8")
        camera = (
            ROOT / "firmware" / "src" / "services" / "camera_light_apply.c"
        ).read_text(encoding="utf-8")
        micropython = (
            ROOT / "firmware" / "src" / "services" / "micropython_binding_service.c"
        ).read_text(encoding="utf-8")
        sleep = (
            ROOT / "firmware" / "src" / "apps" / "sleep" / "sleep_controller.c"
        ).read_text(encoding="utf-8")
        self.assertIn("hk_lights_set_level", settings + camera + micropython)
        self.assertIn("hk_lights_set_rgb", settings + camera + micropython)
        self.assertIn("screen_brightness_off", sleep)
        self.assertNotIn("drivers/hk_lights.h", micropython)
        self.assertNotIn("lights_illum_set", settings + camera + micropython)
        self.assertNotIn("lights_rgb_set", settings + camera + micropython)


if __name__ == "__main__":
    unittest.main()
