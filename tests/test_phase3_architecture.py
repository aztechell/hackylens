from __future__ import annotations

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
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
            "sdk/include/hackylens/app/context.h": "sdk",
            "sdk/include/hackylens/app/runtime.h": "sdk",
            "firmware/src/app_runtime/runtime.c": "app-runtime",
            "firmware/src/apps/example/app.toml": "manifest",
            "firmware/generated/app_registry/registry.c":
                "generated-app-registry",
            "firmware/generated/app_composition/composition.json":
                "generated-app-composition",
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
        sdk = "sdk/include/hackylens/app/context.h"
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

    def test_public_context_and_runtime_headers_are_c_and_cpp_compatible(self) -> None:
        header = "\n".join(
            (ROOT / relative).read_text(encoding="utf-8")
            for relative in (
                "sdk/include/hackylens/app/context.h",
                "sdk/include/hackylens/app/runtime.h",
            )
        )
        for forbidden in (
            "runtime_private.h", "capability_provider.h", "platforms/",
            "firmware/src/", "hal_", "driver", "provider_vtable", "screen_t",
        ):
            self.assertNotIn(forbidden, header)

        source = (
            "#include <hackylens/app/runtime.h>\n"
            "#ifdef __cplusplus\n"
            "static_assert(HK_APP_CONTEXT_MAX_CAPABILITIES == 16U);\n"
            "static_assert(HK_APP_CONTEXT_MAX_SERVICES == 16U);\n"
            "static_assert(HK_APP_MAX_INVALIDATIONS == 8U);\n"
            "int main(void) { hk_app_context_t value = {}; hk_app_event_t event = {}; "
            "return value.owner.slot + static_cast<int>(event.sequence); }\n"
            "#else\n"
            "_Static_assert(HK_APP_CONTEXT_MAX_CAPABILITIES == 16U, \"caps\");\n"
            "_Static_assert(HK_APP_CONTEXT_MAX_SERVICES == 16U, \"services\");\n"
            "_Static_assert(HK_APP_MAX_INVALIDATIONS == 8U, \"invalidations\");\n"
            "int main(void) { hk_app_context_t value = {0}; "
            "hk_app_event_t event = {0}; return value.owner.slot + (int)event.sequence; }\n"
            "#endif\n"
        )
        compilers = (
            (os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc"), "c", "c11"),
            (os.environ.get("CXX") or shutil.which("g++") or shutil.which("c++"), "cc", "c++17"),
        )
        with tempfile.TemporaryDirectory(prefix="hackylens-sdk-context-") as temp:
            for compiler, suffix, standard in compilers:
                self.assertIsNotNone(compiler, f"host {suffix} compiler is required")
                path = Path(temp) / f"context.{suffix}"
                output = Path(temp) / f"context-{suffix}.o"
                path.write_text(source, encoding="utf-8")
                subprocess.run(
                    [
                        str(compiler), f"-std={standard}", "-Wall", "-Wextra",
                        "-Werror", f"-I{ROOT / 'sdk' / 'include'}",
                        f"-I{ROOT / 'firmware' / 'include'}", "-c", str(path),
                        "-o", str(output),
                    ],
                    cwd=ROOT,
                    check=True,
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
