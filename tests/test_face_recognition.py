import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import gen_board


def test_face_recognition_and_png_workspace():
    compiler = os.environ.get("CC") or shutil.which("gcc")
    assert compiler
    app = ROOT / "firmware/src/apps/face_recognition"
    files = ROOT / "firmware/src/apps/files"
    services = ROOT / "firmware/src/services"
    storage = ROOT / "firmware/src/storage"
    core = ROOT / "firmware/src/core"
    with tempfile.TemporaryDirectory(prefix="face-recognition-") as directory:
        output = Path(directory) / ("face-test.exe" if os.name == "nt" else "face-test")
        fixture = Path(directory) / "pixels.zlib"
        fixture.write_bytes(zlib.compress(bytes(i // 7 % 256 for i in range(4096))))
        subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            f"-I{app}", f"-I{files}", f"-I{services}", f"-I{storage}", f"-I{core}",
            f"-I{ROOT / 'firmware/include'}", f"-I{gen_board.board_config_include_dir()}",
            str(ROOT / "tests/face_recognition_harness.c"),
            str(app / "face_recognition_image.c"), str(files / "image_decode_png_inflate.c"),
            str(services / "frame_pool.c"), str(storage / "ai_model_storage.c"),
            str(storage / "face_gallery.c"),
            str(core / "hk_binary.c"), "-lm", "-o", str(output),
        ], check=True)
        result = subprocess.run([str(output), str(fixture)], check=True, text=True, capture_output=True)
        assert "FACE_RECOGNITION_OK" in result.stdout
