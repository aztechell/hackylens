from __future__ import annotations

import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import board_contract
import firmware_attestation
import firmware_sidecar
import gen_board
import hkflash
import service_bindings


class CubeBoardTests(unittest.TestCase):
    def setUp(self):
        self.board = board_contract.load_board("sipeed-maix-cube")

    def test_full_composition_has_all_typed_services_and_twelve_apps(self):
        board_contract.validate_board_source(self.board)
        selected = service_bindings.select(self.board, set(), {"micropython", "camera"}, set())
        self.assertEqual({binding.name for binding in selected.bindings},
                         {"time", "input", "display", "lights", "external-link", "power"})
        self.assertFalse(selected.disabled_apps)
        self.assertFalse(selected.absences)
        self.assertEqual(self.board.runtime_profile, "hackylens-cube")
        self.assertFalse(self.board.releaseable)

    def test_physical_routes_and_driver_choices_are_explicit(self):
        by_macro = {r["macro"]: r for r in self.board.selected_routes()}
        self.assertNotIn("IO_LCD_MOSI", by_macro)
        self.assertNotIn("IO_BTN_BACK", by_macro)
        self.assertEqual(by_macro["IO_BTN_OK"]["pin"], 10)
        self.assertEqual(by_macro["IO_PMIC_SCL"]["peripheral"], "i2c1")
        self.assertEqual(by_macro["IO_PMIC_SDA"]["pin"], 31)
        self.assertEqual(by_macro["IO_CAM_SCCB_SCLK"]["pin"], 41)
        self.assertEqual(by_macro["IO_CAM_SCCB_SDA"]["pin"], 40)
        self.assertEqual(self.board.data["defaults"]["camera_sccb_address"], 0x21)
        self.assertEqual(self.board.data["defaults"]["lcd_spi_hz"], 15_000_000)
        self.assertEqual(self.board.data["defaults"]["camera_sccb_hz"], 100_000)
        self.assertEqual(by_macro["IO_CAM_SCCB_SCLK"]["function"], "i2c2-sclk")
        self.assertEqual(by_macro["IO_EXTERNAL_I2C_R"]["peripheral"], "i2c0")
        self.assertEqual(by_macro["IO_EXTERNAL_UART_R"]["pin"], 24)
        self.assertEqual(by_macro["IO_EXTERNAL_UART_T"]["pin"], 25)
        text = gen_board.render_board_config(self.board)
        for macro in ("HK_BOARD_LCD_PARALLEL8", "HK_BOARD_CAMERA_OV7740",
                      "HK_BOARD_POWER_AXP173", "HK_BOARD_INPUT_PROFILE_CUBE"):
            self.assertIn(f"#define {macro} 1U", text)
        self.assertIn("#define HK_BOARD_UI_WIDTH 240U", text)
        data = copy.deepcopy(self.board.data)
        data["drivers"]["camera"] = "ov2640"
        with self.assertRaisesRegex(board_contract.ContractError, "drivers"):
            board_contract.validate_board_data(data)

    def test_mux_cannot_move_or_shrink_and_pmic_cannot_share_it(self):
        for action in ("move", "shrink", "pmic"):
            data = copy.deepcopy(self.board.data)
            route = next(r for r in data["routes"] if r["macro"] == "IO_EXTERNAL_I2C_R")
            if action == "move":
                route["pin"] = 23
            elif action == "shrink":
                data["routes"].remove(route)
            else:
                next(r for r in data["routes"] if r["macro"] == "IO_PMIC_SCL")["pin"] = 24
            with self.assertRaises(board_contract.ContractError):
                board_contract.validate_board_data(data)

    def test_camera_mounting_orientation_is_explicit_and_validated(self):
        for board_id, rotate in (("sipeed-maix-cube", 1), ("huskylens-sen0305", 0)):
            with self.subTest(board=board_id):
                board = board_contract.load_board(board_id)
                self.assertEqual(board.data["defaults"]["camera_preview_rotate"], rotate)
                self.assertIn(f"#define CAMERA_PREVIEW_ROTATE {rotate}U",
                              gen_board.render_board_config(board))
        for invalid in (-1, 2, True):
            data = copy.deepcopy(self.board.data)
            data["defaults"]["camera_preview_rotate"] = invalid
            with self.subTest(invalid=invalid), self.assertRaises(board_contract.ContractError):
                board_contract.validate_board_data(data)

    def test_userfs_is_only_qualified_for_exactly_sixteen_mib(self):
        self.assertEqual(self.board.flash["minimum_capacity"], 16 * 1024 * 1024)
        self.assertEqual(self.board.flash["maximum_capacity"], 16 * 1024 * 1024)
        self.assertEqual([p["name"] for p in self.board.partitions],
                         ["firmware", "settings_0", "settings_1", "legacy_reserved", "userfs"])
        for part in self.board.partitions:
            self.assertEqual(part["required_capacity"], 16 * 1024 * 1024)
        args = hkflash.build_parser().parse_args([
            "flash", "cube.bin", "--board", self.board.id, "--port", "TEST"])
        hkflash.apply_board_configuration(args)
        self.assertFalse(args.manual)
        self.assertEqual(args.usb_detection_mode, "explicit-port")
        self.assertEqual(args.reset_profile, "sipeed-goe")
        self.assertEqual(args.reset_attempts, 15)
        self.assertIs(args.reset_connector, hkflash.connect_bootrom_sipeed_goe)
        self.assertIs(args.reboot_fallback_handler, hkflash.reset_to_boot_sipeed_goe)

        manual = hkflash.build_parser().parse_args([
            "flash", "cube.bin", "--board", self.board.id, "--port", "TEST", "--manual"])
        hkflash.apply_board_configuration(manual)
        self.assertTrue(manual.manual)

    def test_experimental_image_has_sidecar_but_cannot_claim_release_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "cube.bin"
            image.write_bytes(b"exact experimental build bytes")
            document = firmware_attestation.build_document(
                image, self.board, target="full", disabled_apps=set(), exclusions=[],
                capabilities_sha256="a" * 64)
            self.assertFalse(document["release_qualified"])
            firmware_attestation.validate_document(document, image, self.board)
            with self.assertRaisesRegex(firmware_attestation.AttestationError, "release"):
                firmware_attestation.validate_document(
                    document, image, self.board, require_release_qualified=True)
            sidecar = firmware_sidecar.build_document(image, self.board)
            firmware_sidecar.validate_document(sidecar, image, self.board)
            self.assertEqual(sidecar["runtime_profile"], "hackylens-cube")
            document["release_qualified"] = True
            with self.assertRaises(firmware_attestation.AttestationError):
                firmware_attestation.validate_document(document, image, self.board)


if __name__ == "__main__":
    unittest.main()
