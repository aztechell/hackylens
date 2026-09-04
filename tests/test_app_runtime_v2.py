from __future__ import annotations

from pathlib import Path
import os
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


HOST_RUNTIME_SOURCES = (
    "firmware/src/app_runtime/surface.c",
    "firmware/src/app_runtime/switch.c",
    "firmware/src/capabilities/capability_core.c",
    "firmware/src/capabilities/time.c",
    "firmware/src/capabilities/input.c",
    "firmware/src/capabilities/input_state.c",
    "tests/time_normative_fake_backend.c",
    "tests/input_normative_fake_backend.c",
    "tests/capability_fake_display.c",
    "tests/app_runtime_host_support.c",
    "tests/fixtures/app_sdk/minimal_app.c",
)

HOST_RUNTIME_INCLUDES = (
    ROOT / "firmware" / "src" / "capabilities",
    ROOT / "tests",
    ROOT / "tests" / "fixtures" / "app_sdk",
)


class AppRuntimeV2Tests(unittest.TestCase):
    def compile_and_run_harness(
        self,
        source_name: str,
        extra_sources: tuple[str, ...] = (),
        extra_includes: tuple[Path, ...] = (),
    ) -> subprocess.CompletedProcess[str]:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="hackylens-app-runtime-") as temp:
            executable = Path(temp) / (
                "app_runtime.exe" if os.name == "nt" else "app_runtime"
            )
            subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'sdk' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src'}",
                    f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                    *(f"-I{path}" for path in extra_includes),
                    str(ROOT / "tests" / source_name),
                    str(ROOT / "firmware" / "src" / "app_runtime" / "runtime.c"),
                    *(str(ROOT / source) for source in extra_sources),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                check=True,
            )
            return subprocess.run(
                [str(executable)],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
                timeout=30,
            )

    def test_normative_lifecycle_and_latency(self) -> None:
        result = self.compile_and_run_harness("app_runtime_v2_harness.c")
        match = re.fullmatch(
            r"APP_RUNTIME_V2_OK host_event_p99_ns=(\d+) "
            r"host_launch_p99_ns=(\d+) host_stop_p99_ns=(\d+) limit_us=100 "
            r"samples=101 iterations=1000\n?",
            result.stdout,
        )
        self.assertIsNotNone(match, result.stdout)
        assert match is not None
        for measured_ns in match.groups():
            self.assertLessEqual(int(measured_ns), 100_000)

    def test_manifest_exact_grants_and_owner_retirement(self) -> None:
        result = self.compile_and_run_harness("app_runtime_grants_harness.c")
        self.assertEqual(result.stdout, "APP_RUNTIME_GRANTS_OK\n")

    def test_mixed_runtime_switch_events_and_failure_paths(self) -> None:
        result = self.compile_and_run_harness(
            "app_runtime_mixed_harness.c",
            (
                "firmware/src/app_runtime/surface.c",
                "firmware/src/app_runtime/switch.c",
            ),
        )
        self.assertEqual(result.stdout, "APP_RUNTIME_MIXED_OK\n")

    def test_host_support_executes_production_runtime(self) -> None:
        result = self.compile_and_run_harness(
            "app_runtime_host_cases.c",
            HOST_RUNTIME_SOURCES,
            HOST_RUNTIME_INCLUDES,
        )
        self.assertEqual(result.stdout, "APP_RUNTIME_HOST_OK\n")

    def test_full_firmware_production_integration_harness(self) -> None:
        result = self.compile_and_run_harness(
            "app_runtime_production_harness.c",
            (
                "firmware/src/app_runtime/surface.c",
                "firmware/src/app_runtime/switch.c",
                "firmware/src/runtime/app_runtime_integration.c",
                "firmware/src/runtime/hk_main.c",
            ),
        )
        self.assertEqual(result.stdout, "APP_RUNTIME_PRODUCTION_OK\n")

    def test_owner_and_preflight_authority_stay_private(self) -> None:
        private_header = (
            ROOT / "firmware/src/app_runtime/runtime_private.h"
        ).read_text(encoding="utf-8")
        public_runtime = (
            ROOT / "sdk/include/hackylens/app/runtime.h"
        ).read_text(encoding="utf-8")

        self.assertRegex(
            public_runtime,
            r"typedef hk_result_t \(\*hk_app_start_fn\)\("
            r"const hk_app_context_t \*ctx\);",
        )
        self.assertRegex(
            public_runtime,
            r"typedef hk_result_t \(\*hk_app_event_fn\)\(",
        )
        self.assertRegex(
            public_runtime,
            r"typedef hk_result_t \(\*hk_app_render_fn\)\(",
        )
        self.assertRegex(
            public_runtime,
            r"typedef hk_result_t \(\*hk_app_stop_fn\)\("
            r"const hk_app_context_t \*ctx\);",
        )
        self.assertIn("hk_app_context_request_close", public_runtime)
        self.assertNotIn("hk_app_probe_fn", public_runtime)
        self.assertNotIn("hk_app_prepare_fn", public_runtime)
        self.assertNotIn("hk_app_tick_fn", public_runtime)
        self.assertNotIn("hk_app_cleanup_fn", public_runtime)
        self.assertNotIn("typedef hk_result_t (*hk_app_cleanup_fn)", private_header)
        self.assertIn("hk_owner_t owner;", private_header)
        self.assertIn(
            "uint8_t resolved_available[HK_APP_CONTEXT_MAX_CAPABILITIES];",
            private_header,
        )

    def test_generated_descriptor_carries_manifest_state_size_and_abi_alignment(self) -> None:
        import app_composition
        import app_registry

        generated = app_registry.generated_source(
            app_composition.load_model(),
            app_composition.enable_definition,
        )
        app_count = generated.count(".struct_version = HK_APP_DESCRIPTOR_VERSION")
        self.assertGreater(app_count, 0)
        self.assertEqual(
            generated.count("HK_APP_DESCRIPTOR_STATE_ALIGNMENT,"), app_count
        )


if __name__ == "__main__":
    unittest.main()
