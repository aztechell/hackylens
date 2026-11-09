import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AudioFilesTests(unittest.TestCase):
    def test_recording_directory_scan_distinguishes_end_from_fat_errors(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = ROOT / "firmware/src"
        with tempfile.TemporaryDirectory(prefix="hackylens-fat-dir-") as directory:
            output = Path(directory) / ("scan.exe" if os.name == "nt" else "scan")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware/include'}", f"-I{source / 'storage'}",
                f"-I{source / 'core'}",
                str(ROOT / "tests/fat32_dir_scan_harness.c"),
                str(source / "core/hk_binary.c"), str(source / "core/hk_string.c"),
                str(source / "storage/fat32_alloc.c"), str(source / "storage/file_dir_scan.c"),
                "-o", str(output),
            ], check=True, cwd=ROOT)
            subprocess.run([str(output)], check=True, timeout=10)

    def test_sd_read_token_timeout_and_bus_retry(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="hackylens-sd-read-") as directory:
            output = Path(directory) / ("sd.exe" if os.name == "nt" else "sd")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'platforms/k210/hal'}",
                str(ROOT / "tests/sd_card_harness.c"),
                str(ROOT / "firmware/src/storage/sd_card.c"), "-o", str(output),
            ], check=True, cwd=ROOT)
            subprocess.run([str(output)], check=True, timeout=10)

    def test_sd_probe_requires_consecutive_failures(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = ROOT / "firmware/src"
        with tempfile.TemporaryDirectory(prefix="hackylens-sd-probe-") as directory:
            output = Path(directory) / ("sd.exe" if os.name == "nt" else "sd")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{source / 'services'}",
                str(ROOT / "tests/sd_service_harness.c"),
                str(source / "services/sd_service.c"), "-o", str(output),
            ], check=True, cwd=ROOT)
            subprocess.run([str(output)], check=True, timeout=10)

    def test_wav_playback_owner_and_partial_sd_write(self):
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = ROOT / "firmware/src"
        with tempfile.TemporaryDirectory(prefix="hackylens-audio-") as directory:
            temporary = Path(directory)
            (temporary / "board_config.h").write_text(
                "#define HK_BOARD_AUDIO_ES8374 1\n", encoding="utf-8"
            )
            output = temporary / ("audio.exe" if os.name == "nt" else "audio")
            includes = [
                temporary, ROOT / "firmware/include", ROOT / "sdk/include",
                ROOT / "platforms/k210/hal", source / "core",
                source / "storage", source / "services",
            ]
            files = [
                ROOT / "tests/audio_files_harness.c",
                source / "core/hk_binary.c",
                source / "services/wav_pcm.c",
                source / "services/audio_service.c",
                source / "services/audio_player.c",
                source / "storage/fat32_append.c",
            ]
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                *(f"-I{item}" for item in includes),
                *(str(item) for item in files), "-o", str(output),
            ], check=True, cwd=ROOT)
            result = subprocess.run([str(output)], check=True, capture_output=True,
                                    text=True, timeout=10)
            self.assertIn("AUDIO_FILES_OK", result.stdout)
