import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import app_composition
import build_firmware


EXPECTED_APPS = {
    "terminal", "camera", "qr-camera", "face-detect", "apriltag",
    "object-detect", "files", "buttons", "pong", "settings", "sleep",
    "micropython",
}


class AppCompositionTests(unittest.TestCase):
    def test_repository_manifests_are_complete_deterministic_and_fresh(self) -> None:
        first = app_composition.load_model()
        second = app_composition.load_model()
        self.assertEqual(first, second)
        self.assertEqual({app["id"] for app in first["apps"]}, EXPECTED_APPS)
        self.assertEqual(app_composition.freshness_failures(), [])
        generated = json.loads(
            app_composition.GENERATED_JSON.read_text(encoding="utf-8")
        )
        self.assertEqual(
            [app["id"] for app in generated["apps"]],
            sorted(EXPECTED_APPS),
        )

    def test_enable_definitions_and_source_sets_are_manifest_derived(self) -> None:
        document = app_composition.generated_document(
            app_composition.load_model()
        )
        by_id = {app["id"]: app for app in document["apps"]}
        self.assertEqual(by_id["qr-camera"]["enable_definition"], "HK_ENABLE_APP_QR_CAMERA")
        self.assertIn(
            "firmware/src/apps/qr_camera/qr_decoder_engine.c",
            by_id["qr-camera"]["sources"],
        )
        defaults = app_composition.generated_defaults(app_composition.load_model())
        self.assertEqual(defaults.count("#define HK_ENABLE_APP_"), len(EXPECTED_APPS))
        source = (ROOT / "tools" / "build_firmware.py").read_text(encoding="utf-8")
        self.assertNotIn('"qr-camera": "HK_ENABLE_APP_QR_CAMERA"', source)
        self.assertNotIn('"qr-camera": Path("firmware/src/apps/qr_camera")', source)

    def test_legacy_build_constraints_are_manifest_services_only(self) -> None:
        requirements = build_firmware.load_app_requirements()
        self.assertEqual(requirements["camera"], {"camera", "sd-card"})
        self.assertEqual(requirements["micropython"], {"internal-flash"})
        self.assertFalse((ROOT / "firmware" / "app_requirements.toml").exists())
        for app in app_composition.load_model()["apps"]:
            for service in app["services"]:
                if service["id"].startswith(app_composition.LEGACY_SERVICE_PREFIX):
                    self.assertEqual(app["lifecycle"], "legacy")

    def test_undeclared_translation_unit_is_rejected_before_build(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "apps"
            shutil.copytree(
                ROOT / "tests" / "fixtures" / "app_manifests" / "valid",
                root,
            )
            extra = root / "alpha-tool" / "src" / "undeclared.c"
            extra.write_text("int undeclared;\n", encoding="utf-8")
            with self.assertRaisesRegex(
                app_composition.CompositionError, "manifest sources must exactly cover"
            ):
                app_composition.load_model(root)

    def test_disabled_app_private_sources_leave_staging(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            build_firmware.stage_firmware_sources(stage, {"qr-camera"})
            self.assertFalse(
                (stage / "firmware" / "src" / "apps" / "qr_camera").exists()
            )
            self.assertTrue(
                (stage / "firmware" / "src" / "apps" / "camera" / "camera_app.c").is_file()
            )

    def test_generated_composition_freshness_rejects_stale_output(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            generated_json = Path(directory) / "composition.json"
            generated_defaults = Path(directory) / "app_config_defaults.h"
            with mock.patch.object(app_composition, "GENERATED_JSON", generated_json), \
                    mock.patch.object(app_composition, "GENERATED_DEFAULTS", generated_defaults):
                app_composition.write_generated()
                self.assertEqual(app_composition.freshness_failures(), [])
                generated_json.write_text("{}\n", encoding="utf-8")
                self.assertRegex(
                    "\n".join(app_composition.freshness_failures()),
                    r"composition\.json: generated file is stale",
                )

    def test_release_ci_enforces_manifest_composition(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("check_app_manifests.py --scan-root firmware/src/apps", workflow)
        self.assertIn("gen_app_composition.py --check", workflow)
        self.assertGreaterEqual(workflow.count("check_app_composition.py"), 4)
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("firmware/generated/app_composition/*.json text eol=lf", attributes)
        self.assertIn("firmware/config/app_config_defaults.h text eol=lf", attributes)


if __name__ == "__main__":
    unittest.main()
