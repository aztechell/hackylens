from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import check_arch


class Phase2ArchitectureGuardTests(unittest.TestCase):
    def test_explicit_policy_classifies_every_production_source(self) -> None:
        policy = check_arch.load_layer_policy()
        unclassified = [
            check_arch.repository_relative(path)
            for path in check_arch.repository_source_files(policy)
            if check_arch.classify_repository_path(
                check_arch.repository_relative(path), policy
            ) is None
        ]
        self.assertEqual(unclassified, [])
        self.assertEqual(
            check_arch.classify_repository_path(
                r"FIRMWARE\SRC\APPS\demo\drivers\misleading.h", policy
            ),
            "app",
        )

    def test_phase2_negative_edges_are_hard_rules(self) -> None:
        cases = (
            ("firmware/src/apps/demo/app.c", "firmware/src/drivers/device.h"),
            ("firmware/src/apps/demo/app.c", "boards/example/board.c"),
            ("firmware/src/adapters/micropython/binding.c",
             "platforms/k210/hal/hal_i2c.h"),
            ("firmware/src/capabilities/display.c",
             "firmware/src/apps/files/files_app.h"),
            ("firmware/src/services/shared.c",
             "firmware/src/apps/camera/camera_private.h"),
            ("boards/example/board.c", "firmware/src/ui/hk_ui.h"),
        )
        for source, target in cases:
            with self.subTest(source=source, target=target):
                self.assertIsNotNone(
                    check_arch.layer_edge_violation(source, target)
                )
        self.assertIsNone(check_arch.layer_edge_violation(
            "firmware/src/apps/demo/app.c",
            "firmware/include/hackylens/capability/display.h",
        ))

    def test_forwarding_headers_and_symlink_targets_do_not_hide_drivers(self) -> None:
        graph = {
            "firmware/src/apps/demo/app.c": ["firmware/src/core/forward.h"],
            "firmware/src/core/forward.h": ["firmware/src/drivers/private.h"],
            "firmware/src/drivers/private.h": [],
        }
        failures = check_arch.transitive_layer_failures(graph)
        self.assertTrue(any("transitive dependency" in item for item in failures))

        with tempfile.TemporaryDirectory(prefix="hackylens-arch-symlink-") as temp:
            repository = Path(temp)
            app = repository / "firmware" / "src" / "apps" / "demo" / "app.c"
            driver = repository / "firmware" / "src" / "drivers" / "private.h"
            app.parent.mkdir(parents=True)
            driver.parent.mkdir(parents=True)
            app.write_text('#include "forward.h"\n', encoding="utf-8")
            driver.write_text("#pragma once\n", encoding="utf-8")
            forward = app.parent / "forward.h"
            forward.write_text("placeholder\n", encoding="utf-8")
            real_resolve = Path.resolve

            def resolve_with_link(path: Path, *args, **kwargs):
                if path == forward:
                    return real_resolve(driver)
                return real_resolve(path, *args, **kwargs)

            with mock.patch.object(Path, "resolve", resolve_with_link):
                target = check_arch.resolve_repository_include(
                    app, "forward.h", repository_root=repository,
                    source_root=repository / "firmware" / "src",
                )
            self.assertEqual(target, "firmware/src/drivers/private.h")

    def test_generated_dependencies_and_object_symbols_are_policy_inputs(self) -> None:
        dependency = (
            "target.obj: C:\\stage\\firmware\\src\\apps\\demo\\app.c \\\n"
            " C:\\stage\\firmware\\src\\drivers\\private.h\n"
        )
        self.assertEqual(
            check_arch.dependency_repository_paths(dependency),
            [
                "firmware/src/apps/demo/app.c",
                "firmware/src/drivers/private.h",
            ],
        )
        self.assertEqual(
            check_arch.forbidden_hardware_symbols(
                {"hal_hidden_call", "driver_private", "hk_display_present"},
                {"driver_private"},
            ),
            ["driver_private", "hal_hidden_call"],
        )

    def test_inventory_and_python_provider_bypasses_are_detected(self) -> None:
        manual = (
            "static const hk_capability_provider_t *const providers[] = {0};\n"
            "void hk_generated_capability_inventory_get(void) { }\n"
        )
        self.assertEqual(check_arch.manual_provider_inventory_lines(manual), [1, 2])
        self.assertEqual(
            check_arch.python_gated_provider_lines(
                "#if HK_ENABLE_APP_MICROPYTHON\n#endif\n"
            ),
            [1],
        )
        self.assertEqual(
            check_arch.provider_hash_mismatches(
                {"platforms/k210/capabilities/display_adapter.c": "a"},
                {"platforms/k210/capabilities/display_adapter.c": "b"},
            ),
            [
                "platforms/k210/capabilities/display_adapter.c: provider object "
                "differs between full and MicroPython-disabled profiles"
            ],
        )

    def test_frame_pool_borrow_release_contract(self) -> None:
        compiler = os.environ.get("CC") or "gcc"
        with tempfile.TemporaryDirectory(prefix="hackylens-frame-pool-") as temp:
            executable = Path(temp) / (
                "frame_pool.exe" if os.name == "nt" else "frame_pool"
            )
            subprocess.run(
                [
                    compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                    f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                    str(ROOT / "tests" / "frame_pool_harness.c"),
                    str(ROOT / "firmware" / "src" / "services" / "frame_pool.c"),
                    "-o", str(executable),
                ],
                cwd=ROOT, check=True,
            )
            result = subprocess.run(
                [str(executable)], cwd=ROOT, check=True, text=True,
                capture_output=True, timeout=30,
            )
        self.assertIn(
            "FRAME_POOL_OK borrow=exclusive stale=blocked camera=exclusive",
            result.stdout,
        )

    def test_repository_guard_is_green(self) -> None:
        self.assertEqual(check_arch.phase2_source_failures(), [])


if __name__ == "__main__":
    unittest.main()
