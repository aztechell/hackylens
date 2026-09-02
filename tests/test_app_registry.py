from __future__ import annotations

import copy
import os
from pathlib import Path
import re
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
        runtime = (ROOT / "firmware/src/core/hk_app_registry.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("terminal", runtime)
        self.assertNotIn("camera", runtime)
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

    def test_current_legacy_menu_autostart_debug_and_callback_parity(self) -> None:
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
        expected_callbacks = {
            "terminal": {"screen": "HK_TERMINAL_SCREEN", "enter": "terminal_enter", "exit": "terminal_exit", "tick": "terminal_tick", "handle_input": "terminal_handle_buttons", "draw_icon": "terminal_draw_icon"},
            "camera": {"screen": "SCREEN_CAMERA", "enter": "camera_enter", "exit": "camera_exit", "tick": "camera_tick", "handle_input": "camera_handle_buttons", "owns_screen": "camera_owns_screen", "draw_icon": "camera_draw_icon", "blocks_sd_poll": "1U", "handle_debug_command": "camera_handle_debug_command"},
            "qr-camera": {"screen": "SCREEN_QR_CAMERA", "enter": "qr_camera_enter", "exit": "qr_camera_exit", "tick": "qr_camera_tick", "handle_input": "qr_camera_handle_buttons", "owns_screen": "qr_camera_owns_screen", "draw_icon": "qr_camera_draw_icon", "blocks_sd_poll": "1U", "handle_debug_command": "qr_camera_handle_debug_command"},
            "face-detect": {"screen": "SCREEN_FACE_DETECT", "enter": "face_detect_enter", "exit": "face_detect_exit", "tick": "face_detect_tick", "handle_input": "face_detect_handle_buttons", "draw_icon": "face_detect_draw_icon", "background_tick": "face_detect_background_tick", "blocks_sd_poll": "1U", "handle_debug_command": "face_detect_handle_debug_command"},
            "apriltag": {"screen": "SCREEN_APRILTAG", "enter": "apriltag_enter", "exit": "apriltag_exit", "tick": "apriltag_tick", "handle_input": "apriltag_handle_buttons", "draw_icon": "apriltag_draw_icon", "background_tick": "apriltag_background_tick", "handle_debug_command": "apriltag_handle_debug_command"},
            "object-detect": {"screen": "SCREEN_OBJECT_DETECT", "enter": "object_detect_enter", "exit": "object_detect_exit", "tick": "object_detect_tick", "handle_input": "object_detect_handle_buttons", "draw_icon": "object_detect_draw_icon", "background_tick": "object_detect_background_tick", "blocks_sd_poll": "1U", "handle_debug_command": "object_detect_handle_debug_command"},
            "files": {"screen": "SCREEN_FILES", "enter": "files_enter", "exit": "files_exit", "tick": "files_tick", "handle_input": "files_handle_buttons", "draw_icon": "files_draw_icon", "handle_sd_event": "files_handle_sd_event"},
            "buttons": {"screen": "SCREEN_BUTTONS", "enter": "buttons_enter", "tick": "buttons_tick", "handle_input": "buttons_handle_buttons", "draw_icon": "buttons_draw_icon"},
            "pong": {"screen": "HK_PONG_SCREEN", "enter": "pong_enter", "tick": "pong_tick", "handle_input": "pong_handle_buttons", "draw_icon": "pong_draw_icon"},
            "settings": {"screen": "SCREEN_SETTINGS", "enter": "settings_enter", "exit": "settings_exit", "tick": "settings_tick", "handle_input": "settings_handle_buttons", "draw_icon": "settings_draw_icon", "handle_debug_command": "settings_handle_debug_command"},
            "sleep": {"screen": "SCREEN_SLEEP", "enter": "sleep_enter", "handle_input": "sleep_handle_buttons", "draw_icon": "sleep_draw_icon", "background_tick": "sleep_background_tick", "blocks_sd_poll": "1U"},
            "micropython": {"screen": "HK_MICROPYTHON_SCREEN", "enter": "micropython_enter", "exit": "micropython_exit", "tick": "micropython_tick", "handle_input": "micropython_handle_buttons", "draw_icon": "micropython_draw_icon", "background_tick": "micropython_background_tick", "handle_debug_command": "micropython_handle_debug_command"},
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
                source = (
                    app_composition.MANIFEST_ROOT / app["directory"] /
                    f"{app['directory']}_app.c"
                ).read_text(encoding="utf-8")
                match = re.search(
                    rf"const hk_legacy_app_entry_t {re.escape(app['entry'])} = "
                    r"\{(?P<body>[\s\S]*?)\n\};",
                    source,
                )
                self.assertIsNotNone(match)
                actual = dict(re.findall(
                    r"\.(\w+)\s*=\s*([^,\n]+)", match.group("body")
                ))
                self.assertEqual(actual, expected_callbacks[app_id])


if __name__ == "__main__":
    unittest.main()
