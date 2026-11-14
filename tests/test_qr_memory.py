import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QrMemoryTests(unittest.TestCase):
    def test_micropython_decodes_qr_and_apriltag_from_camera_leases(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory(prefix="micropython-codes-") as directory:
            temp = Path(directory)
            executable = temp / ("codes.exe" if os.name == "nt" else "codes")
            (temp / "hk_config.h").write_text(
                "#define HK_ENABLE_APP_QR_CAMERA 1\n#define HK_ENABLE_APP_APRILTAG 1\n",
                encoding="utf-8",
            )
            services = ROOT / "firmware/src/services"
            quirc = ROOT / "firmware/third_party/quirc"
            apriltag = ROOT / "firmware/third_party/apriltag"
            subprocess.run([
                compiler, "-std=gnu11", "-O2", f"-I{temp}",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                f"-I{ROOT / 'firmware/include'}", f"-I{quirc}", f"-I{apriltag}",
                str(ROOT / "tests/micropython_codes_harness.c"),
                str(ROOT / "firmware/src/adapters/micropython/micropython_camera_codes.c"),
                str(ROOT / "firmware/src/apps/apriltag/apriltag_distance.c"),
                *(str(services / name) for name in (
                    "qr_decoder_engine.c", "qr_luma.c", "apriltag_decoder.c",
                    "vision_result_service.c",
                )),
                *(str(path) for path in sorted(quirc.glob("*.c"))),
                *(str(path) for path in sorted(apriltag.rglob("*.c"))),
                "-lm", "-o", str(executable),
            ], check=True)
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("MICROPYTHON_CODES_OK", result.stdout)

    def test_async_decoder_owns_luma_after_camera_reuse_and_exit(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory(prefix="qr-async-") as directory:
            temp = Path(directory)
            executable = temp / ("qr-async.exe" if os.name == "nt" else "qr-async")
            (temp / "board_config.h").write_text("", encoding="utf-8")
            app = ROOT / "firmware/src/apps/qr_camera"
            services = ROOT / "firmware/src/services"
            quirc = ROOT / "firmware/third_party/quirc"
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra",
                "-DHACKYLENS_VERSION=0", f"-I{temp}",
                f"-I{ROOT / 'firmware/include'}", f"-I{ROOT / 'firmware/assets'}",
                f"-I{ROOT / 'sdk/include'}",
                f"-I{quirc}", str(ROOT / "tests/qr_async_harness.c"),
                *(str(app / name) for name in (
                    "qr_service.c", "qr_camera_frame_adapter.c", "qr_result_state.c",
                )),
                str(services / "qr_decoder_engine.c"),
                str(services / "qr_luma.c"),
                *(str(quirc / name) for name in (
                    "quirc.c", "identify.c", "decode.c", "version_db.c",
                )),
                str(ROOT / "firmware/src/services/resource_cleanup.c"),
                str(ROOT / "firmware/src/services/vision_result_service.c"),
                "-lm", "-o", str(executable),
            ], check=True)
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("QR_ASYNC_OK", result.stdout)

    def test_decoder_returns_all_heap_on_close_and_reopens_after_failure(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory(prefix="qr-memory-") as directory:
            temp = Path(directory)
            executable = temp / ("qr-memory.exe" if os.name == "nt" else "qr-memory")
            includes = ["firmware/third_party/quirc", "firmware/include"]
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                *(f"-I{ROOT / path}" for path in includes),
                str(ROOT / "tests/qr_memory_harness.c"),
                "-o", str(executable),
            ], check=True)
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("QR_MEMORY_OK", result.stdout)
