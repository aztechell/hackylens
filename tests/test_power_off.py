import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PowerOffTests(unittest.TestCase):
    def test_sen_disabled_off_and_restart(self):
        self.run_board(0)

    def test_single_press_and_ordered_shutdown_failures(self):
        self.run_board(1)

    def run_board(self, axp173):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-power-off-") as temp:
            temp = Path(temp)
            (temp / "board_config.h").write_text(f"#define HK_BOARD_POWER_AXP173 {axp173}\n")
            (temp / "hk_config.h").write_text("#define HK_ENABLE_APP_MICROPYTHON 1\n")
            executable = temp / ("power_off.exe" if os.name == "nt" else "power_off")
            subprocess.run([compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{temp}", f"-I{ROOT / 'firmware/assets'}", f"-I{ROOT / 'firmware/include'}", f"-I{ROOT / 'sdk/include'}", f"-I{ROOT / 'platforms/k210/hal'}",
                str(ROOT / "tests/power_off_harness.c"),
                str(ROOT / "firmware/src/apps/sleep/power_app.c"),
                str(ROOT / "firmware/src/capabilities/power.c"),
                str(ROOT / "firmware/src/services/power_shutdown.c"),
                str(ROOT / "platforms/k210/capabilities/power_adapter.c"),
                "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True, timeout=30)
            self.assertIn("POWER_OFF_OK", result.stdout)

    def test_real_k210_surface_budget_and_present(self):
        import sys
        sys.path.insert(0,str(ROOT / "tools"))
        import gen_board, board_contract
        compiler=shutil.which("gcc") or shutil.which("cc")
        if not compiler: self.skipTest("host compiler unavailable")
        for board in ("sipeed-maix-cube", "huskylens-sen0305"):
            with self.subTest(board=board), tempfile.TemporaryDirectory(prefix="power-real-") as directory:
                temp=Path(directory)
                gen_board.write_board_config(board_contract.load_board(board),temp / "board_config.h")
                executable=temp / "power-real.exe"
                includes=[temp,ROOT / "firmware/include",ROOT / "sdk/include",ROOT / "tests",
                    ROOT / "firmware/src/capabilities",ROOT / "firmware/src/drivers",ROOT / "firmware/src/ui",
                    ROOT / "firmware/src/config",ROOT / "firmware/src/core",ROOT / "platforms/k210/hal",ROOT / "firmware/assets"]
                sources=["tests/power_display_harness.c","tests/display_normative_suite.c",
                    "platforms/k210/capabilities/display_adapter.c","firmware/src/services/frame_pool.c",
                    "firmware/src/capabilities/display.c","firmware/src/ui/hk_font.c",
                    "firmware/src/app_runtime/surface.c","firmware/src/apps/sleep/power_app.c",
                    "firmware/src/ui/ui_menu.c","firmware/src/ui/display_binding.c"]
                subprocess.run([compiler,"-std=c11","-O1","-Wall","-Wextra","-Werror","-DK210_DISPLAY_ADAPTER_TESTING","-Wno-sign-compare","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",
                    *[f"-I{p}" for p in includes],*[str(ROOT / p) for p in sources],"-o",str(executable)],check=True,cwd=ROOT)
                result=subprocess.run([str(executable)],check=True,capture_output=True,text=True,cwd=ROOT,timeout=30)
                self.assertIn("POWER_REAL_DISPLAY_OK",result.stdout)
