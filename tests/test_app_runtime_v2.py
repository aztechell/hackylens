from __future__ import annotations

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AppRuntimeV2Tests(unittest.TestCase):
    def test_normative_lifecycle_and_latency(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="hackylens-app-runtime-") as temp:
            executable = Path(temp) / (
                "app_runtime_v2.exe" if os.name == "nt" else "app_runtime_v2"
            )
            subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    f"-I{ROOT / 'firmware' / 'src'}",
                    str(ROOT / "tests" / "app_runtime_v2_harness.c"),
                    str(ROOT / "firmware" / "src" / "app_runtime" / "runtime.c"),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                check=True,
            )
            result = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
                timeout=30,
            )
        match = re.fullmatch(
            r"APP_RUNTIME_V2_OK host_dispatch_p99_ns=(\d+) limit_us=100 "
            r"samples=101 iterations=1000\n?",
            result.stdout,
        )
        self.assertIsNotNone(match, result.stdout)
        assert match is not None
        self.assertLessEqual(int(match.group(1)), 100_000)

    def test_engine_remains_private_and_unwired_from_production_dispatch(self) -> None:
        main = (ROOT / "firmware/src/runtime/hk_main.c").read_text(encoding="utf-8")
        menu = (ROOT / "firmware/src/core/hk_app_registry.c").read_text(
            encoding="utf-8"
        )
        for source in (main, menu):
            self.assertNotIn("hk_app_runtime_", source)
            self.assertNotIn("app_runtime/runtime", source)

        runtime = (ROOT / "firmware/src/app_runtime/runtime.c").read_text(
            encoding="utf-8"
        )
        for forbidden in (
            "malloc(",
            "calloc(",
            "realloc(",
            "xTaskCreate",
            "xQueueCreate",
            "hal_core1_start",
            "hal_time",
        ):
            self.assertNotIn(forbidden, runtime)

    def test_generated_descriptor_carries_manifest_state_size_and_abi_alignment(self) -> None:
        generated = (ROOT / "firmware/generated/app_registry/registry.c").read_text(
            encoding="utf-8"
        )
        app_count = generated.count(".struct_version = HK_APP_DESCRIPTOR_VERSION")
        self.assertGreater(app_count, 0)
        self.assertEqual(generated.count("HK_APP_STATE_ALIGNMENT,"), app_count)
        self.assertIn("uint32_t state_alignment;", (
            ROOT / "firmware/src/core/hk_app.h"
        ).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
