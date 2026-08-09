import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "hackylens_build_firmware", ROOT / "tools" / "build_firmware.py"
)
assert SPEC is not None and SPEC.loader is not None
BUILD_FIRMWARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD_FIRMWARE)
PACKAGE_SPEC = importlib.util.spec_from_file_location(
    "hackylens_package_release", ROOT / "tools" / "package_release.py"
)
assert PACKAGE_SPEC is not None and PACKAGE_SPEC.loader is not None
PACKAGE_RELEASE = importlib.util.module_from_spec(PACKAGE_SPEC)
PACKAGE_SPEC.loader.exec_module(PACKAGE_RELEASE)
HKFLASH_SPEC = importlib.util.spec_from_file_location(
    "hackylens_hkflash", ROOT / "tools" / "hkflash.py"
)
assert HKFLASH_SPEC is not None and HKFLASH_SPEC.loader is not None
HKFLASH = importlib.util.module_from_spec(HKFLASH_SPEC)
HKFLASH_SPEC.loader.exec_module(HKFLASH)
SYMBOL_SPEC = importlib.util.spec_from_file_location(
    "hackylens_check_firmware_symbols",
    ROOT / "tools" / "check_firmware_symbols.py",
)
assert SYMBOL_SPEC is not None and SYMBOL_SPEC.loader is not None
CHECK_SYMBOLS = importlib.util.module_from_spec(SYMBOL_SPEC)
SYMBOL_SPEC.loader.exec_module(CHECK_SYMBOLS)


class BuildContractsTest(unittest.TestCase):
    def test_generated_config_uses_canonical_version(self):
        expected = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            BUILD_FIRMWARE.write_config(stage, set())
            config = (stage / "hk_config.h").read_text(encoding="utf-8")
        self.assertIn(f'#define HACKYLENS_VERSION "{expected}"', config)
        self.assertIn("#define HK_MICROPYTHON_WDT_FAULT_INJECTION 0", config)

    def test_wdt_fault_injection_is_explicit_and_test_build_only(self):
        expected = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            BUILD_FIRMWARE.write_config(stage, set(), True)
            config = (stage / "hk_config.h").read_text(encoding="utf-8")
        self.assertIn("#define HK_MICROPYTHON_WDT_FAULT_INJECTION 1", config)
        self.assertIn(f'#define HACKYLENS_VERSION "{expected}-wdtfi"', config)

    def test_release_packager_rejects_wdt_fault_injection_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"prefix 0.2.0-wdtfi suffix")
            with self.assertRaisesRegex(SystemExit, "fault-injection"):
                PACKAGE_RELEASE.validate_release_firmware(image, "0.2.0")
            image.write_bytes(b"prefix 0.2.0 suffix")
            PACKAGE_RELEASE.validate_release_firmware(image, "0.2.0")

    def test_build_package_and_flasher_share_canonical_partition(self):
        firmware = PACKAGE_RELEASE._FIRMWARE_PARTITION
        expected_limit = firmware["offset"] + firmware["size"]
        self.assertEqual(BUILD_FIRMWARE.FIRMWARE_FLASH_LIMIT, expected_limit)
        self.assertEqual(HKFLASH.FLASH_ADDRESS, firmware["offset"])
        self.assertEqual(HKFLASH.FIRMWARE_FLASH_LIMIT, expected_limit)
        self.assertEqual(
            BUILD_FIRMWARE.FLASH_ERASE_SIZE, HKFLASH.FLASH_ERASE_SIZE
        )

    def test_flasher_rejects_payload_past_firmware_partition(self):
        size = HKFLASH.FIRMWARE_FLASH_LIMIT - HKFLASH.FLASH_ADDRESS
        self.assertEqual(HKFLASH.firmware_erase_length(size), size)
        with self.assertRaisesRegex(ValueError, "canonical partition"):
            HKFLASH.firmware_erase_length(size + 1)
        with self.assertRaisesRegex(ValueError, "empty"):
            HKFLASH.firmware_erase_length(0)

    def test_release_output_rejects_stray_fault_image(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "hackylens-wdtfi.bin").write_bytes(b"danger")
            with self.assertRaisesRegex(SystemExit, "unexpected files"):
                PACKAGE_RELEASE.ensure_allowlisted_output(
                    output, {"hackylens-v0.2.0.bin"}
                )

    def test_generated_dependency_manifest_lists_actual_embed_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            embed = Path(directory)
            (embed / "py").mkdir()
            (embed / "port").mkdir()
            (embed / "py" / "runtime.c").write_text("", encoding="utf-8")
            (embed / "port" / "embed_util.c").write_text("", encoding="utf-8")
            (embed / "port" / "mphalport.c").write_text("", encoding="utf-8")
            manifest = PACKAGE_RELEASE.firmware_dependency_manifest(
                "0.2.0", embed
            )
        dependencies = manifest["dependencies"]
        self.assertEqual(dependencies[0]["revision"], PACKAGE_RELEASE.MICROPYTHON_REVISION)
        self.assertEqual(
            dependencies[0]["included_source_files"],
            ["port/embed_util.c", "py/runtime.c"],
        )
        self.assertEqual(dependencies[1]["revision"], PACKAGE_RELEASE.LITTLEFS_REVISION)

    def test_firmware_symbol_feature_gate(self):
        symbols = {
            "mp_embed_init",
            "mp_builtin_max_obj",
            "mp_builtin_min_obj",
            "mp_builtin_sum_obj",
            "micropython_runtime_start",
            "hmpy_session_begin",
            "userfs_mount",
        }
        groups = CHECK_SYMBOLS.verify(symbols, "present")
        self.assertTrue(all(groups.values()))
        CHECK_SYMBOLS.verify({"main", "camera_tick"}, "absent")
        with self.assertRaisesRegex(ValueError, "leaked symbols"):
            CHECK_SYMBOLS.verify(symbols, "absent")
        with self.assertRaisesRegex(ValueError, "missing symbol groups"):
            CHECK_SYMBOLS.verify({"mp_embed_init"}, "present")
        with self.assertRaisesRegex(ValueError, "missing required symbols"):
            CHECK_SYMBOLS.verify(
                {
                    "mp_embed_init",
                    "micropython_runtime_start",
                    "hmpy_session_begin",
                    "userfs_mount",
                },
                "present",
            )

    def test_invalid_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            version_file = Path(directory) / "VERSION"
            version_file.write_text('0.2.0"\\n#define BAD 1', encoding="utf-8")
            with self.assertRaises(RuntimeError):
                BUILD_FIRMWARE.read_firmware_version(version_file)

    def test_native_iterator_patch_applies_to_pinned_runtime(self):
        source = ROOT / "_deps" / "micropython" / "py" / "runtime.c"
        if not source.is_file():
            self.skipTest("pinned MicroPython checkout is not bootstrapped")
        if shutil.which("git") is None:
            self.skipTest("git is unavailable")
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            package = Path(directory)
            (package / "py").mkdir()
            shutil.copy2(source, package / "py" / "runtime.c")
            BUILD_FIRMWARE.apply_micropython_native_poll_patch(package)
            patched = (package / "py" / "runtime.c").read_text(encoding="utf-8")
        self.assertEqual(patched.count("MICROPY_PORT_ITERNEXT_HOOK"), 2)
        self.assertIn(
            "mp_obj_t mp_iternext_allow_raise(mp_obj_t o_in) {\n"
            "    MICROPY_PORT_ITERNEXT_HOOK",
            patched,
        )
        self.assertIn(
            "mp_obj_t mp_iternext(mp_obj_t o_in) {\n"
            "    MICROPY_PORT_ITERNEXT_HOOK",
            patched,
        )

    def test_watchdog_reset_cause_guards_startup(self):
        watchdog = (ROOT / "firmware" / "src" / "hal" / "hal_watchdog.c").read_text(
            encoding="utf-8"
        )
        autostart = (
            ROOT / "firmware" / "src" / "controllers" / "autostart_controller.c"
        ).read_text(encoding="utf-8")
        app = (
            ROOT / "firmware" / "src" / "apps" / "micropython" /
            "micropython_app.c"
        ).read_text(encoding="utf-8")
        self.assertNotIn("SYSCTL_RESET_SOC", watchdog)
        self.assertIn("SYSCTL_RESET_STATUS_WDT1", watchdog)
        self.assertIn("WDT_DEVICE_1", watchdog)
        self.assertIn("hal_watchdog_force_reset", watchdog)
        self.assertNotIn("wdt_feed", watchdog)
        self.assertNotIn("wdt_clear_interrupt", watchdog)
        self.assertIn("hal_watchdog_reset_detected()", autostart)
        self.assertIn("g_startup[0] && !watchdog_recovery", app)


if __name__ == "__main__":
    unittest.main()
