from __future__ import annotations

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import app_manifest as MANIFEST


VALID_FIXTURES = ROOT / "tests" / "fixtures" / "app_manifests" / "valid"
ALPHA = Path("alpha-tool") / "app.toml"


class AppManifestSchemaTests(unittest.TestCase):
    def copy_fixtures(self, destination: Path) -> Path:
        root = destination / "valid"
        shutil.copytree(VALID_FIXTURES, root)
        return root

    def reject_alpha(self, old: str, new: str, expected: str) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-") as temp:
            root = self.copy_fixtures(Path(temp))
            path = root / ALPHA
            source = path.read_text(encoding="utf-8")
            changed = source.replace(old, new, 1)
            self.assertNotEqual(source, changed, f"mutation did not match: {old!r}")
            path.write_text(changed, encoding="utf-8")
            with self.assertRaisesRegex(MANIFEST.ManifestError, expected):
                MANIFEST.validate_tree(root)

    def collision_root(self, destination: Path, field: str) -> Path:
        root = self.copy_fixtures(destination)
        alpha = root / "alpha-tool"
        beta = root / "beta-tool"
        shutil.copytree(alpha, beta)
        path = beta / "app.toml"
        source = path.read_text(encoding="utf-8")
        source = source.replace("alpha-tool", "beta-tool")
        source = source.replace("alpha_tool_app", "beta_tool_app")
        source = source.replace("menu_order = 10", "menu_order = 11")
        source = source.replace("autostart_id = 42", "autostart_id = 43")
        if field == "id":
            source = source.replace('id = "beta-tool"', 'id = "alpha-tool"', 1)
        elif field == "entry":
            source = source.replace("beta_tool_app", "alpha_tool_app", 1)
        elif field == "menu.order":
            source = source.replace("menu_order = 11", "menu_order = 10", 1)
        elif field == "autostart.id":
            source = source.replace("autostart_id = 43", "autostart_id = 42", 1)
        else:
            self.fail(f"unknown collision field {field}")
        path.write_text(source, encoding="utf-8")
        return root

    def test_committed_positive_fixtures_have_path_independent_canonical_model(self) -> None:
        first = MANIFEST.validate_tree(VALID_FIXTURES)
        first_bytes = MANIFEST.canonical_json_bytes(first)
        self.assertEqual([item["id"] for item in first["apps"]], [
            "alpha-tool", "zeta-legacy",
        ])
        alpha = first["apps"][0]
        self.assertEqual(alpha["generated_symbol"], "hk_generated_app_alpha_tool")
        self.assertEqual(alpha["menu"]["order"], 10)
        self.assertTrue(alpha["menu"]["visible"])
        self.assertEqual(alpha["autostart"]["id"], 42)
        self.assertEqual(
            alpha["capabilities"]["optional"][0]["id"],
            "hackylens.cap.display",
        )
        self.assertEqual(
            alpha["capabilities"]["optional"][0]["fallback"],
            "headless",
        )
        self.assertEqual(
            first["apps"][1]["sources"],
            ["src/zeta_legacy.c", "src/zeta_view.c"],
        )
        self.assertFalse(first["apps"][1]["autostart"]["eligible"])
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-copy-") as temp:
            copied = self.copy_fixtures(Path(temp))
            second_bytes = MANIFEST.canonical_json_bytes(
                MANIFEST.validate_tree(copied)
            )
        self.assertEqual(first_bytes, second_bytes)
        self.assertEqual(first, json.loads(first_bytes.decode("utf-8")))

    def test_cli_validates_all_manifests_and_emits_identical_canonical_bytes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-cli-") as temp:
            first = Path(temp) / "first.json"
            second = Path(temp) / "second.json"
            command = [
                sys.executable,
                str(TOOLS / "check_app_manifests.py"),
                "--scan-root",
                str(VALID_FIXTURES),
            ]
            one = subprocess.run(
                command + ["--output", str(first)],
                cwd=ROOT, text=True, capture_output=True, timeout=30,
            )
            two = subprocess.run(
                command + ["--output", str(second)],
                cwd=ROOT, text=True, capture_output=True, timeout=30,
            )
            self.assertEqual(one.returncode, 0, one.stderr)
            self.assertEqual(two.returncode, 0, two.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_cli_rejects_empty_or_malformed_input_without_emitting_a_model(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-bad-cli-") as temp:
            temporary = Path(temp)
            empty = temporary / "empty"
            empty.mkdir()
            malformed = temporary / "malformed" / "app"
            malformed.mkdir(parents=True)
            (malformed / "app.toml").write_text("id = [\n", encoding="utf-8")
            for scan_root in (empty, malformed.parent):
                with self.subTest(scan_root=scan_root):
                    output = temporary / f"{scan_root.name}.json"
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(TOOLS / "check_app_manifests.py"),
                            "--scan-root", str(scan_root),
                            "--output", str(output),
                        ],
                        cwd=ROOT, text=True, capture_output=True, timeout=30,
                    )
                    self.assertEqual(result.returncode, 1)
                    self.assertIn("[FAIL] Native App Manifest", result.stderr)
                    self.assertFalse(output.exists())

    def test_unknown_and_missing_fields_are_rejected(self) -> None:
        mutations = (
            ("id = \"alpha-tool\"\n", 'id = "alpha-tool"\nunknown = 7\n', "unknown=unknown"),
            ('id = "alpha-tool"\n', "", "missing=id"),
            ('entry = "alpha_tool_app"\n', "", "missing=entry"),
            ("tick_ms = 10\n", "", "missing=tick_ms"),
            ('requires = ["input", "settings"]\n', "", "missing=requires"),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_identity_lifecycle_menu_and_autostart_are_strict(self) -> None:
        mutations = (
            ('id = "alpha-tool"', 'id = "Alpha-Tool"', "invalid value"),
            ('id = "alpha-tool"', 'id = "alpha_tool"', "invalid value"),
            ('id = "alpha-tool"', f'id = "a{"a" * 63}"', "exceeds 63"),
            ('name = "Alpha Tool"', 'name = " Alpha Tool"', "trimmed string"),
            ('entry = "alpha_tool_app"', 'entry = "alpha-tool-app"', "invalid value"),
            ('lifecycle = "v2"', 'lifecycle = "dynamic"', "lifecycle must be one of"),
            ("menu_order = 10", "menu_order = 0", "menu_order: outside"),
            ("autostart_id = 42", "autostart_id = 65536", "autostart_id: outside"),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_omitted_menu_order_hides_the_app(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-hidden-") as temp:
            root = self.copy_fixtures(Path(temp))
            path = root / ALPHA
            path.write_text(
                path.read_text(encoding="utf-8").replace("menu_order = 10\n", ""),
                encoding="utf-8",
            )
            model = MANIFEST.validate_tree(root)
        alpha = next(app for app in model["apps"] if app["id"] == "alpha-tool")
        self.assertFalse(alpha["menu"]["visible"])
        self.assertEqual(alpha["menu"]["order"], 0)

    def test_omitted_autostart_id_is_ineligible(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-off-") as temp:
            root = self.copy_fixtures(Path(temp))
            path = root / ALPHA
            path.write_text(
                path.read_text(encoding="utf-8").replace("autostart_id = 42\n", ""),
                encoding="utf-8",
            )
            model = MANIFEST.validate_tree(root)
        alpha = next(app for app in model["apps"] if app["id"] == "alpha-tool")
        self.assertFalse(alpha["autostart"]["eligible"])
        self.assertEqual(alpha["autostart"]["id"], 0)

    def test_explicit_zero_autostart_id_is_ineligible(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-zero-") as temp:
            root = self.copy_fixtures(Path(temp))
            path = root / ALPHA
            path.write_text(
                path.read_text(encoding="utf-8").replace(
                    "autostart_id = 42", "autostart_id = 0"
                ),
                encoding="utf-8",
            )
            model = MANIFEST.validate_tree(root)
        alpha = next(app for app in model["apps"] if app["id"] == "alpha-tool")
        self.assertFalse(alpha["autostart"]["eligible"])
        self.assertEqual(alpha["autostart"]["id"], 0)

    def test_required_services_and_optional_fallbacks_are_strict(self) -> None:
        mutations = (
            (
                'requires = ["input", "settings"]',
                'requires = ["input", "not-a-service"]',
                "unknown required service",
            ),
            (
                'requires = ["input", "settings"]',
                'requires = ["hackylens.cap.input"]',
                "invalid value",
            ),
            (
                'requires = ["input", "settings"]',
                'requires = ["input", "input"]',
                "duplicate value",
            ),
            (
                'optional = ["display"]',
                'optional = ["display", "display"]',
                "duplicate value",
            ),
            (
                'optional = ["display"]',
                'optional = ["input"]',
                "cannot be both required and optional",
            ),
            (
                'optional = ["display"]',
                'optional = ["sd-card"]',
                "services cannot be optional",
            ),
            (
                'optional = ["display"]',
                "optional = []",
                "must not be empty",
            ),
            (
                'requires = ["input", "settings"]',
                'requires = ["input", "camera"]',
                "transitional legacy services require lifecycle=legacy",
            ),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_debug_text_is_bounded_when_present(self) -> None:
        self.reject_alpha(
            'debug = "alpha-status"',
            'debug = "' + ("d" * 1025) + '"',
            "debug: exceeds 1024 UTF-8 bytes",
        )

    def test_tick_period_rejects_zero_and_over_limit_values(self) -> None:
        mutations = (
            ("tick_ms = 10", "tick_ms = 0", "tick_ms: outside"),
            ("tick_ms = 10", "tick_ms = -1", "tick_ms: outside"),
            ("tick_ms = 10", 'tick_ms = "20"', "tick_ms: expected integer"),
            ("tick_ms = 10", "tick_ms = 60001", "tick_ms: outside"),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_paths_reject_traversal_absolute_alternate_case_and_wrong_kind(self) -> None:
        mutations = (
            ('"src/alpha_tool.c"', '"../outside.c"', "traversal"),
            ('"src/alpha_tool.c"', '"/absolute.c"', "absolute paths"),
            ('"src/alpha_tool.c"', '"C:/absolute.c"', "forward-slash relative"),
            ('"src/alpha_tool.c"', '"src\\\\alpha_tool.c"', "forward-slash relative"),
            ('"src/alpha_tool.c"', '"src/./alpha_tool.c"', "traversal"),
            ('"src/alpha_tool.c"', '"src/missing.c"', "does not exist"),
            ('"src/alpha_tool.c"', '"src/ALPHA_TOOL.c"', "path case|does not exist"),
            ('"src/alpha_tool.c"', '"src"', "expected file"),
            ('"src/alpha_tool.c"', '"include/alpha_tool.h"', "expected one of"),
            ('sources = ["src/alpha_tool.c"]', "sources = []", "sources: must not be empty"),
            ('private_includes = ["include"]', 'private_includes = ["src/alpha_tool.c"]', "expected directory"),
            ('private_includes = ["include"]', "private_includes = []", "omit empty arrays"),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_resolved_symlink_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-manifest-link-") as temp:
            temporary = Path(temp)
            root = self.copy_fixtures(temporary)
            app = root / "alpha-tool"
            outside = temporary / "outside.c"
            outside.write_text("void outside(void) {}\n", encoding="utf-8")
            linked = app / "src" / "escape.c"
            path = app / "app.toml"
            path.write_text(
                path.read_text(encoding="utf-8").replace(
                    '"src/alpha_tool.c"', '"src/escape.c"', 1
                ),
                encoding="utf-8",
            )
            try:
                linked.symlink_to(outside)
            except OSError:
                linked.write_text("fixture\n", encoding="utf-8")
                original = MANIFEST._resolve_existing

                def resolved(candidate: Path) -> Path:
                    if candidate == linked:
                        return outside.resolve(strict=True)
                    return original(candidate)

                context = mock.patch.object(
                    MANIFEST, "_resolve_existing", side_effect=resolved
                )
            else:
                context = mock.patch.object(
                    MANIFEST, "_resolve_existing", wraps=MANIFEST._resolve_existing
                )
            with context, self.assertRaisesRegex(MANIFEST.ManifestError, "escapes"):
                MANIFEST.validate_tree(root)

    def test_collection_rejects_every_identity_and_order_collision(self) -> None:
        for field in ("id", "entry", "menu.order", "autostart.id"):
            with self.subTest(field=field), tempfile.TemporaryDirectory(
                prefix="hackylens-app-manifest-collision-"
            ) as temp:
                root = self.collision_root(Path(temp), field)
                with self.assertRaisesRegex(
                    MANIFEST.ManifestError, rf"collision for {re.escape(field)}"
                ):
                    MANIFEST.validate_tree(root)

    def test_firmware_does_not_parse_manifests_at_runtime(self) -> None:
        firmware_text = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (ROOT / "firmware").rglob("*")
            if path.is_file() and path.suffix.casefold() in {".c", ".h", ".cpp"}
        )
        self.assertNotIn("app.toml", firmware_text)
        self.assertNotIn("toml_parse", firmware_text.casefold())
        self.assertNotIn("tomllib", firmware_text.casefold())


if __name__ == "__main__":
    unittest.main()
