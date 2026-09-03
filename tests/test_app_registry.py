from __future__ import annotations

import copy
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import app_composition
import app_registry


class AppRegistryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.model = app_composition.load_model()

    def compiler(self) -> str:
        configured = os.environ.get("CC")
        if configured and Path(configured).is_file():
            return configured
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.fail("host C compiler is required for registry tests")
        return compiler

    def compile_profile(self, *, micropython: bool) -> str:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-registry-") as directory:
            temporary = Path(directory)
            definitions = []
            for app in self.model["apps"]:
                enabled = micropython or app["id"] != "micropython"
                definitions.append(
                    f"#define {app_composition.enable_definition(app['id'])} "
                    f"{1 if enabled else 0}"
                )
            (temporary / "hk_config.h").write_text(
                "\n".join(definitions) + "\n", encoding="utf-8"
            )
            executable = temporary / (
                "app_registry.exe" if os.name == "nt" else "app_registry"
            )
            subprocess.run([
                self.compiler(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                f"-I{temporary}",
                f"-I{ROOT / 'firmware' / 'src'}",
                str(ROOT / "firmware/generated/app_registry/registry.c"),
                str(ROOT / "firmware/src/core/hk_app_registry.c"),
                str(ROOT / "tests/app_registry_harness.c"),
                "-o", str(executable),
            ], cwd=ROOT, check=True)
            result = subprocess.run(
                [str(executable)], cwd=ROOT, check=False, capture_output=True,
                text=True, timeout=30,
            )
            if result.returncode != 0:
                self.fail(
                    f"registry harness failed ({result.returncode}): "
                    f"stdout={result.stdout!r} stderr={result.stderr!r}"
                )
        return result.stdout

    def compile_fixture_registry(self, *, hidden_enabled: bool) -> str:
        buttons = copy.deepcopy(next(
            app for app in self.model["apps"] if app["id"] == "buttons"
        ))
        terminal = copy.deepcopy(next(
            app for app in self.model["apps"] if app["id"] == "terminal"
        ))
        buttons["menu"] = {"visible": False, "order": 65000}
        buttons["autostart"] = {"eligible": True, "id": 42}
        terminal["autostart"] = {"eligible": True, "id": 1}
        model = {"schema": 1, "apps": [buttons, terminal]}

        with tempfile.TemporaryDirectory(prefix="hackylens-app-registry-fixture-") as directory:
            temporary = Path(directory)
            core = temporary / "firmware/src/core"
            generated = temporary / "firmware/generated/app_registry"
            core.mkdir(parents=True)
            generated.mkdir(parents=True)
            for name in ("hk_app_registry.c", "hk_app_registry.h", "hk_app.h", "hk_events.h"):
                shutil.copy2(ROOT / "firmware/src/core" / name, core / name)
            (generated / "registry.h").write_text(
                app_registry.generated_header(model), encoding="utf-8"
            )
            (generated / "registry.c").write_text(
                app_registry.generated_source(
                    model, app_composition.enable_definition
                ),
                encoding="utf-8",
            )
            (temporary / "hk_config.h").write_text(
                "#define HK_ENABLE_APP_BUTTONS "
                f"{1 if hidden_enabled else 0}\n"
                "#define HK_ENABLE_APP_TERMINAL 1\n",
                encoding="utf-8",
            )
            harness = temporary / "fixture.c"
            harness.write_text(r'''
#include <stdio.h>
#include <string.h>
#include "hk_config.h"
#include "firmware/generated/app_registry/registry.h"
#include "firmware/src/core/hk_app_registry.h"

const hk_legacy_app_entry_t buttons_legacy_entry = {0};
const hk_legacy_app_entry_t terminal_legacy_entry = {0};

int main(void)
{
    if(g_hk_reserved_autostart_id_count != 2U ||
       g_hk_reserved_autostart_ids[0] != 1U ||
       g_hk_reserved_autostart_ids[1] != 42U ||
       !hk_app_autostart_id_is_persistable(0U) ||
       !hk_app_autostart_id_is_persistable(1U) ||
       !hk_app_autostart_id_is_persistable(42U) ||
       hk_app_autostart_id_is_persistable(17U) ||
       hk_app_autostart_id_is_persistable(41U))
        return 1;
#if HK_ENABLE_APP_BUTTONS
    if(g_menu_item_count != 1U || hk_app_autostart_count() != 2U ||
       !hk_app_for_autostart_id(42U) ||
       strcmp(hk_app_autostart_at(0U)->id, "buttons") != 0)
        return 2;
#else
    if(g_menu_item_count != 1U || hk_app_autostart_count() != 1U ||
       hk_app_for_autostart_id(42U) != NULL ||
       !hk_app_autostart_id_is_persistable(42U))
        return 3;
#endif
    puts("FIXTURE_REGISTRY_OK");
    return 0;
}
''', encoding="utf-8")
            executable = temporary / (
                "fixture_registry.exe" if os.name == "nt" else "fixture_registry"
            )
            subprocess.run([
                self.compiler(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                f"-I{temporary}", str(generated / "registry.c"),
                str(core / "hk_app_registry.c"), str(harness), "-o", str(executable),
            ], cwd=ROOT, check=True)
            result = subprocess.run(
                [str(executable)], cwd=ROOT, check=False, capture_output=True,
                text=True, timeout=30,
            )
            if result.returncode != 0:
                self.fail(
                    f"fixture registry failed ({result.returncode}): "
                    f"stdout={result.stdout!r} stderr={result.stderr!r}"
                )
        return result.stdout

    def test_all_apps_registry_runtime(self) -> None:
        self.assertIn("APP_REGISTRY_OK apps=12 menu=12", self.compile_profile(
            micropython=True
        ))

    def test_disabled_app_leaves_registry_and_stable_id_becomes_unavailable(self) -> None:
        self.assertIn("APP_REGISTRY_OK apps=11 menu=11", self.compile_profile(
            micropython=False
        ))

    def test_empty_and_single_registry_generation_are_bounded(self) -> None:
        empty = {"schema": 1, "apps": []}
        empty_header = app_registry.generated_header(empty)
        empty_source = app_registry.generated_source(
            empty, app_composition.enable_definition
        )
        self.assertIn("g_hk_reserved_autostart_ids", empty_header)
        self.assertEqual(empty_source.count("    NULL,"), 2)
        self.assertIn("    HK_AUTOSTART_OFF,", empty_source)
        self.assertNotIn("extern const hk_legacy_app_entry_t", empty_source)

        terminal = next(
            app for app in self.model["apps"] if app["id"] == "terminal"
        )
        single = {"schema": 1, "apps": [copy.deepcopy(terminal)]}
        single_source = app_registry.generated_source(
            single, app_composition.enable_definition
        )
        self.assertEqual(single_source.count("const hk_app_t hk_generated_app_terminal"), 1)
        self.assertEqual(single_source.count("&hk_generated_app_terminal,"), 2)
        self.assertNotIn("hk_generated_app_camera", single_source)

    def test_autostart_identity_is_independent_of_menu_visibility_and_order(self) -> None:
        fixture = copy.deepcopy(next(
            app for app in self.model["apps"] if app["id"] == "buttons"
        ))
        fixture["menu"] = {"visible": False, "order": 65000}
        fixture["autostart"] = {"eligible": True, "id": 42}
        source = app_registry.generated_source(
            {"schema": 1, "apps": [fixture]},
            app_composition.enable_definition,
        )
        menu = source.split(
            "const hk_app_t *const g_menu_items[] = {", 1
        )[1].split(
            "const uint8_t g_menu_item_count =", 1
        )[0]
        self.assertNotIn("&hk_generated_app_buttons,", menu)
        self.assertIn(".autostart_id = 42U", source)
        self.assertIn(".autostart_eligible = 1U", source)
        self.assertIn("    42U,", source)

    def test_sparse_ids_reject_holes_and_hidden_app_remains_enumerable(self) -> None:
        self.assertIn(
            "FIXTURE_REGISTRY_OK", self.compile_fixture_registry(hidden_enabled=True)
        )

    def test_disabled_app_keeps_reserved_persisted_identity(self) -> None:
        self.assertIn(
            "FIXTURE_REGISTRY_OK", self.compile_fixture_registry(hidden_enabled=False)
        )

    def test_mixed_legacy_and_v2_entries_are_typed(self) -> None:
        terminal = copy.deepcopy(next(
            app for app in self.model["apps"] if app["id"] == "terminal"
        ))
        buttons = copy.deepcopy(next(
            app for app in self.model["apps"] if app["id"] == "buttons"
        ))
        buttons["lifecycle"] = "v2"
        buttons["entry"] = "buttons_v2_entry"
        source = app_registry.generated_source(
            {"schema": 1, "apps": [buttons, terminal]},
            app_composition.enable_definition,
        )
        self.assertIn(
            "extern const hk_legacy_app_entry_t terminal_legacy_entry;", source
        )
        self.assertIn("extern const hk_app_v2_entry_t buttons_v2_entry;", source)
        self.assertIn("{.legacy = &terminal_legacy_entry}", source)
        self.assertIn("{.v2 = &buttons_v2_entry}", source)

    def test_fixture_manifest_changes_generated_output_not_generic_runtime(self) -> None:
        fixture = copy.deepcopy(next(
            app for app in self.model["apps"] if app["id"] == "buttons"
        ))
        fixture["id"] = "fixture-app"
        fixture["entry"] = "fixture_legacy_entry"
        fixture["generated_symbol"] = "hk_generated_app_fixture"
        fixture["menu"]["order"] = 99
        fixture["autostart"] = {"eligible": False, "id": 0}
        source = app_registry.generated_source(
            {"schema": 1, "apps": [fixture]},
            app_composition.enable_definition,
        )
        self.assertIn("fixture_legacy_entry", source)
        self.assertIn("hk_generated_app_fixture", source)
        self.assertFalse((ROOT / "firmware/src/apps/app_registry.c").exists())

    def test_generated_registry_is_deterministic_and_policy_free(self) -> None:
        first = app_registry.generated_source(
            self.model, app_composition.enable_definition
        )
        second = app_registry.generated_source(
            copy.deepcopy(self.model), app_composition.enable_definition
        )
        self.assertEqual(first, second)
        for forbidden in (
            "boards/", "platforms/", "hal_", "driver_", "provider_vtable",
        ):
            self.assertNotIn(forbidden, first.casefold())
        self.assertIn("hide-external-link-menu", first)
        self.assertIn("hackylens.service.legacy-camera", first)

    def test_current_legacy_menu_autostart_debug_and_tick_parity(self) -> None:
        expected_order = [
            "terminal", "camera", "qr-camera", "face-detect", "apriltag",
            "object-detect", "files", "buttons", "pong", "settings",
            "sleep", "micropython",
        ]
        expected_autostart = {
            "terminal": 1, "camera": 2, "qr-camera": 3, "face-detect": 4,
            "apriltag": 5, "files": 6, "buttons": 7, "pong": 8,
            "object-detect": 9, "micropython": 10, "settings": 0,
            "sleep": 0,
        }
        expected_debug = {
            "camera": "HKCAMERA",
            "qr-camera": "HKQRINFO HKQR/HKQRCAM HKQRDECODE",
            "face-detect": "HKFACEINFO",
            "apriltag": "HKTAG/HKTAGINFO",
            "object-detect": "HKOBJECT/HKOBJECTINFO",
            "settings": "HKSETTINGS",
            "micropython": (
                "HKMPRUN HKMPTEST HKMPSTOP HKMPSTATUS HKMPLOG HKMPLIST "
                "HKMPFORMAT-CONFIRM"
            ),
        }
        apps = {app["id"]: app for app in self.model["apps"]}
        self.assertEqual([
            app["id"] for app in sorted(
                self.model["apps"], key=lambda app: app["menu"]["order"]
            ) if app["menu"]["visible"]
        ], expected_order)
        for app_id, app in apps.items():
            with self.subTest(app=app_id):
                self.assertEqual(app["autostart"]["id"], expected_autostart[app_id])
                self.assertEqual(
                    app["limits"]["tick_interval_us"],
                    1000 if app_id in {
                        "camera", "qr-camera", "face-detect", "apriltag",
                        "object-detect",
                    } else 5000 if app_id == "micropython" else 20000,
                )
                if app_id in expected_debug:
                    self.assertEqual(app["metadata"]["debug"], expected_debug[app_id])


if __name__ == "__main__":
    unittest.main()
