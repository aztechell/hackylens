from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import board_contract
import hkflash


class IspProfileTests(unittest.TestCase):
    def test_each_board_selects_its_bundled_stub(self):
        for board_id, name in (("huskylens-sen0305", "huskylens"),
                               ("sipeed-maix-cube", "cube")):
            with self.subTest(board=board_id):
                board = board_contract.load_board(board_id)
                expected = f"isp_stub/isp_prog_{name}.bin"
                self.assertEqual(board.data["programming"]["isp_stub"], expected)
                for command in ("flash", "flash-monitor"):
                    args = hkflash.build_parser().parse_args([
                        command, "firmware.bin", "--board", board_id, "--port", "TEST"])
                    hkflash.apply_board_configuration(args)
                    self.assertEqual(Path(args.isp_stub), ROOT / expected)

    def test_explicit_stub_override_is_preserved(self):
        args = hkflash.build_parser().parse_args([
            "flash", "firmware.bin", "--board", "sipeed-maix-cube",
            "--port", "TEST", "--isp-stub", "custom.bin"])
        hkflash.apply_board_configuration(args)
        self.assertEqual(args.isp_stub, "custom.bin")

    def test_bundled_artifacts_match_the_source_manifest(self):
        manifest = json.loads((ROOT / "isp_stub/artifacts.json").read_text())
        self.assertEqual(set(manifest), {"cube", "huskylens"})
        for board, entry in manifest.items():
            with self.subTest(board=board):
                binary = ROOT / "isp_stub" / f"isp_prog_{board}.bin"
                self.assertEqual(entry["file"], binary.name)
                data = binary.read_bytes()
                self.assertEqual(len(data), entry["size"])
                self.assertGreater(len(data), 0)
                self.assertLessEqual(len(data), 64 * 1024)
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])

    def test_missing_stub_fails_before_opening_serial(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"test image")
            args = hkflash.build_parser().parse_args([
                "flash", str(image), "--board", "sipeed-maix-cube", "--port", "TEST",
                "--isp-stub", str(Path(directory) / "missing.bin")])
            hkflash.apply_board_configuration(args)
            with patch.object(hkflash, "validate_image_sidecar"), \
                 patch.object(hkflash, "require_serial") as serial, \
                 contextlib.redirect_stdout(io.StringIO()), \
                 contextlib.redirect_stderr(io.StringIO()) as errors:
                self.assertEqual(hkflash.cmd_flash(args), 1)
                serial.assert_not_called()
                self.assertIn("ISP stub not found", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
