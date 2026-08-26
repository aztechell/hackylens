from __future__ import annotations

from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import check_arch


class Phase3ArchitecturePolicyTests(unittest.TestCase):
    def test_phase3_layers_are_generic_and_unambiguous(self) -> None:
        policy = check_arch.load_layer_policy()
        expected = {
            "sdk/include/hackylens/app.h": "sdk",
            "firmware/src/app_runtime/runtime.c": "app-runtime",
            "firmware/src/apps/example/app.toml": "manifest",
            "firmware/generated/app_registry/registry.c":
                "generated-app-registry",
            "firmware/src/apps/example/example_app.c": "app",
        }
        for path, layer in expected.items():
            with self.subTest(path=path):
                self.assertEqual(
                    check_arch.classify_repository_path(path, policy), layer
                )

        layer_text = (ROOT / "tools" / "architecture_layers.toml").read_text(
            encoding="utf-8"
        )
        for app_id in check_arch.FEATURES:
            self.assertNotIn(
                f'"{app_id}"', layer_text,
                "Phase 3 layer policy must not allowlist concrete apps",
            )

    def test_sdk_can_reuse_public_capability_types_only(self) -> None:
        sdk = "sdk/include/hackylens/app.h"
        self.assertIsNone(check_arch.layer_edge_violation(
            sdk, "firmware/include/hackylens/capability/display.h"
        ))
        forbidden = (
            "firmware/src/capabilities/capability_provider.h",
            "platforms/k210/capabilities/display_adapter.c",
            "firmware/src/services/camera_service.c",
            "firmware/src/drivers/camera_stream.h",
            "boards/huskylens-sen0305/board.c",
            "platforms/k210/hal/hal_dvp.c",
            "firmware/src/app_runtime/runtime_private.h",
        )
        for target in forbidden:
            with self.subTest(target=target):
                self.assertIsNotNone(
                    check_arch.layer_edge_violation(sdk, target)
                )

    def test_runtime_and_generated_registry_do_not_gain_hardware_policy(self) -> None:
        runtime = "firmware/src/app_runtime/runtime.c"
        generated = "firmware/generated/app_registry/registry.c"
        for source in (runtime, generated):
            for target in (
                "boards/huskylens-sen0305/board.c",
                "platforms/k210/hal/hal_dvp.c",
                "firmware/src/drivers/camera_stream.h",
            ):
                with self.subTest(source=source, target=target):
                    self.assertIsNotNone(
                        check_arch.layer_edge_violation(source, target)
                    )

        self.assertIsNone(check_arch.layer_edge_violation(
            generated, "firmware/src/apps/example/example_app.h"
        ))
        self.assertIsNone(check_arch.layer_edge_violation(
            runtime, "firmware/generated/app_registry/registry.h"
        ))


if __name__ == "__main__":
    unittest.main()
