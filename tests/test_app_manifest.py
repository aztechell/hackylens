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
        source = source.replace(
            "hk_generated_app_alpha_tool", "hk_generated_app_beta_tool"
        )
        source = source.replace("order = 10", "order = 11")
        source = source.replace("id = 42", "id = 43")
        if field == "id":
            source = source.replace('id = "beta-tool"', 'id = "alpha-tool"', 1)
            source = source.replace(
                'namespace = "beta-tool.settings"',
                'namespace = "alpha-tool.settings"',
            )
        elif field == "entry":
            source = source.replace("beta_tool_app", "alpha_tool_app", 1)
        elif field == "generated_symbol":
            source = source.replace(
                "hk_generated_app_beta_tool", "hk_generated_app_alpha_tool", 1
            )
        elif field == "menu.order":
            source = source.replace("order = 11", "order = 10", 1)
        elif field == "autostart.id":
            source = source.replace("id = 43", "id = 42", 1)
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
        self.assertEqual(
            first["apps"][0]["capabilities"]["optional"][0]["features"],
            ["base-plane", "rgb565"],
        )
        self.assertEqual(
            first["apps"][1]["sources"],
            ["src/zeta_legacy.c", "src/zeta_view.c"],
        )
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
            (malformed / "app.toml").write_text("schema = [\n", encoding="utf-8")
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

    def test_unknown_and_missing_schema_fields_are_rejected_without_defaults(self) -> None:
        mutations = (
            ("schema = 1", "schema = 2", "schema must be integer 1"),
            ("schema = 1", 'schema = "1"', "schema must be integer 1"),
            ("schema = 1", "schema = true", "schema must be integer 1"),
            ("schema = 1", "schema = 1\nunknown = 7", "unknown=unknown"),
            ('id = "alpha-tool"\n', "", "missing=id"),
            ('entry = "alpha_tool_app"\n', "", "missing=entry"),
            ('generated_symbol = "hk_generated_app_alpha_tool"\n', "", "missing=generated_symbol"),
            ("[menu]", "[menu]\nunknown = true", "menu: unknown=unknown"),
            ("visible = true\n", "", "menu: missing=visible"),
            ("[autostart]", "[autostart]\nunknown = true", "autostart: unknown=unknown"),
            ("[limits]", "[limits]\nunknown = 1", "limits: unknown=unknown"),
            ("state_bytes = 256\n", "", "limits: missing=state_bytes"),
            ("[metadata]", "[metadata]\nunknown = 1", "metadata: unknown=unknown"),
            ("[tests]", "[tests]\nunknown = 1", "tests: unknown=unknown"),
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
            (
                'generated_symbol = "hk_generated_app_alpha_tool"',
                'generated_symbol = "hk-generated-alpha"',
                "invalid value",
            ),
            ('lifecycle = "v2"', 'lifecycle = "dynamic"', "lifecycle must be one of"),
            ("visible = true", "visible = 1", "menu.visible: expected boolean"),
            ("order = 10", "order = 0", "menu.order: outside"),
            ("eligible = true", "eligible = false", "must be non-zero exactly"),
            ("id = 42", "id = 0", "must be non-zero exactly"),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_app_version_accepts_semver_components_above_uint16(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="hackylens-app-manifest-version-"
        ) as temp:
            root = self.copy_fixtures(Path(temp))
            path = root / ALPHA
            source = path.read_text(encoding="utf-8")
            path.write_text(
                source.replace(
                    'version = "0.1.0"', 'version = "65536.0.0"', 1
                ),
                encoding="utf-8",
            )
            model = MANIFEST.validate_tree(root)
        alpha = next(app for app in model["apps"] if app["id"] == "alpha-tool")
        self.assertEqual(alpha["version"], "65536.0.0")

    def test_versions_are_canonical_and_capability_versions_are_bounded(self) -> None:
        mutations = (
            ('version = "0.1.0"', 'version = "01.1.0"', "canonical SemVer"),
            ('version = "0.1.0"', 'version = "0.1"', "canonical SemVer"),
            ('version = "0.1.0"', 'version = "0.1.0-01"', "leading zeroes"),
            (
                'minimum = "0.1.0"', 'minimum = "0.2.0"',
                "minimum must precede maximum_exclusive",
            ),
            (
                'minimum = "0.1.0"', 'minimum = "0.1.0-beta"',
                "canonical MAJOR.MINOR.PATCH",
            ),
            (
                'maximum_exclusive = "0.2.0"',
                'maximum_exclusive = "65536.0.0"',
                "component exceeds uint16",
            ),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_capability_and_optional_fallback_contract_is_strict(self) -> None:
        mutations = (
            (
                'id = "hackylens.cap.input"', 'id = "input"',
                "capabilities.required.*invalid value",
            ),
            ("instance = 0", "instance = 65536", "instance: outside"),
            (
                'features = ["state", "events"]',
                'features = ["state", "state"]',
                "features: duplicate value",
            ),
            (
                'features = ["state", "events"]',
                'features = ["state", "Bad Feature"]',
                "features.*invalid value",
            ),
            (
                ', fallback = "headless"', "",
                "capabilities.optional.*missing=fallback",
            ),
            (
                'fallback = "headless"', 'fallback = ""',
                "expected non-empty trimmed string",
            ),
            (
                'id = "hackylens.cap.display"', 'id = "hackylens.cap.input"',
                "cannot be both required and optional",
            ),
            (
                'features = ["state", "events"] }',
                'features = ["state", "events"], extra = 1 }',
                "unknown=extra",
            ),
            (
                'instance = 0, minimum = "0.1.0"',
                'minimum = "0.1.0"',
                "missing=instance",
            ),
            (
                '  { id = "hackylens.cap.input", instance = 0, minimum = "0.1.0", maximum_exclusive = "0.2.0", features = ["state", "events"] },',
                '  { id = "hackylens.cap.input", instance = 0, minimum = "0.1.0", maximum_exclusive = "0.2.0", features = ["state", "events"] },\n'
                '  { id = "hackylens.cap.input", instance = 0, minimum = "0.1.0", maximum_exclusive = "0.2.0", features = [] },',
                "duplicate capability request",
            ),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_app_scoped_services_and_test_metadata_are_strict(self) -> None:
        mutations = (
            (
                'id = "hackylens.service.settings"', 'id = "settings"',
                "services.*invalid value",
            ),
            (
                'id = "hackylens.service.settings"',
                'id = "hackylens.service.legacy-camera"',
                "transitional legacy services require lifecycle=legacy",
            ),
            (
                'namespace = "alpha-tool.settings"',
                'namespace = "other-app.settings"',
                "must be scoped below",
            ),
            (
                'namespace = "alpha-tool.settings"',
                'namespace = "alpha-tool.Bad"',
                "invalid scoped namespace",
            ),
            (
                '  { id = "hackylens.service.settings", namespace = "alpha-tool.settings" },',
                '  { id = "hackylens.service.settings", namespace = "alpha-tool.settings" },\n'
                '  { id = "hackylens.service.settings", namespace = "alpha-tool.settings" },',
                "duplicate service or namespace",
            ),
            (
                'host_sources = ["tests/alpha_tool_test.c"]',
                "host_sources = []",
                "host_sources: must not be empty",
            ),
            (
                'build_profiles = ["standalone", "full", "disabled"]',
                'build_profiles = ["full", "full"]',
                "duplicate value",
            ),
            (
                'build_profiles = ["standalone", "full", "disabled"]',
                'build_profiles = ["runtime-install"]',
                "unknown=runtime-install",
            ),
            ('help = "Exercises the schema-1 lifecycle-v2 surface."', 'help = ""', "trimmed string"),
            (
                'help = "Exercises the schema-1 lifecycle-v2 surface."',
                'help = "' + ("h" * 1025) + '"',
                "metadata.help: exceeds 1024 UTF-8 bytes",
            ),
            (
                'debug = "alpha-status"',
                'debug = "' + ("d" * 1025) + '"',
                "metadata.debug: exceeds 1024 UTF-8 bytes",
            ),
        )
        for old, new, expected in mutations:
            with self.subTest(old=old, new=new):
                self.reject_alpha(old, new, expected)

    def test_resource_limits_reject_zero_negative_unbounded_and_over_limit_values(self) -> None:
        mutations = (
            ("static_ram_bytes = 4096", "static_ram_bytes = 0", "static_ram_bytes: outside"),
            ("stack_bytes = 2048", "stack_bytes = -1", "stack_bytes: outside"),
            ("state_bytes = 256", 'state_bytes = "unbounded"', "state_bytes: expected integer"),
            ("stack_bytes = 2048", "stack_bytes = 32769", "stack_bytes: outside"),
            ("state_bytes = 256", "state_bytes = 5000", "state_bytes exceeds"),
            ("tick_interval_us = 10000", "tick_interval_us = 0", "tick_interval_us: outside"),
            ("tick_interval_us = 10000", "tick_interval_us = 4294967295", "tick_interval_us: outside"),
            ("tick_budget_us = 1000", "tick_budget_us = 11000", "tick_budget_us exceeds"),
            ("render_budget_us = 2000", "render_budget_us = 1000001", "render_budget_us: outside"),
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
            ('"tests/alpha_tool_test.c"', '"include/alpha_tool.h"', "expected one of"),
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
        for field in (
            "id", "entry", "generated_symbol", "menu.order", "autostart.id",
        ):
            with self.subTest(field=field), tempfile.TemporaryDirectory(
                prefix="hackylens-app-manifest-collision-"
            ) as temp:
                root = self.collision_root(Path(temp), field)
                with self.assertRaisesRegex(
                    MANIFEST.ManifestError, rf"collision for {re.escape(field)}"
                ):
                    MANIFEST.validate_tree(root)

    def test_release_ci_runs_the_build_time_validator(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("check_app_manifests.py --scan-root", workflow)
        self.assertIn("tests/fixtures/app_manifests/valid", workflow)
        self.assertLess(
            workflow.index("check_app_manifests.py --scan-root"),
            workflow.index("build_firmware.py"),
        )
        firmware_text = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (ROOT / "firmware").rglob("*")
            if path.is_file() and path.suffix.casefold() in {".c", ".h", ".cpp"}
        )
        self.assertNotIn("app.toml", firmware_text)
        self.assertNotIn("toml_parse", firmware_text.casefold())


if __name__ == "__main__":
    unittest.main()
