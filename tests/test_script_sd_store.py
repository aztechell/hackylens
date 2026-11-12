import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ScriptSdStoreTests(unittest.TestCase):
    def test_scoped_fat32_publication_and_cleanup(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        self.assertTrue(compiler)
        source = ROOT / "firmware/src"
        with tempfile.TemporaryDirectory(prefix="hackylens-script-sd-") as directory:
            executable = Path(directory) / ("script-sd.exe" if os.name == "nt" else "script-sd")
            modules = (
                "core/hk_binary.c", "core/hk_string.c",
                "services/camera_photo_encode.c", "services/camera_photo_format.c",
                "services/resource_cleanup.c", "services/script_sd_store.c",
                "storage/fat32_alloc.c", "storage/fat32_append.c", "storage/fat32_dir.c",
                "storage/fat32_file.c", "storage/fat32_state.c",
                "storage/file_dir_scan.c", "storage/file_path.c",
            )
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware/include'}",
                str(ROOT / "tests/script_sd_store_harness.c"),
                *(str(source / module) for module in modules),
                "-o", str(executable),
            ], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("SCRIPT_SD_STORE_OK", result.stdout)
