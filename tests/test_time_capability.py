import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TimeCapabilityTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_time_contract_and_bounded_object(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-time-") as temp:
            temporary = Path(temp)
            executable = temporary / (
                "time_capability.exe" if os.name == "nt" else "time_capability"
            )
            time_object = temporary / "time.o"
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'runtime'}",
            ]
            subprocess.run([
                compiler, *common, "-c",
                str(ROOT / "firmware" / "src" / "capabilities" / "time.c"),
                "-o", str(time_object),
            ], check=True, cwd=ROOT)
            subprocess.run([
                compiler, *common,
                str(ROOT / "tests" / "time_capability_harness.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "time.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "capability_core.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(time_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in ("malloc", "calloc", "realloc", "free", "task", "queue"):
                    self.assertNotIn(forbidden, symbols)

        self.assertIn("TIME_CAPABILITY_OK cases=10 max_slice_us=5000", result.stdout)

    def test_all_application_time_calls_use_the_public_provider(self) -> None:
        apps = ROOT / "firmware" / "src" / "apps"
        sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(apps.rglob("*"))
            if path.suffix in {".c", ".h"}
        )
        self.assertNotIn("time_internal", sources)
        self.assertNotIn("hal_time_us", sources)
        self.assertNotIn("hal_sleep", sources)

        bindings = (
            apps / "micropython" / "micropython_bindings.c"
        ).read_text(encoding="utf-8")
        self.assertIn("hk_time_now_us", bindings)
        self.assertIn("hk_time_sleep_until", bindings)
        self.assertIn("micropython_runtime_interrupt_pending", bindings)

        adapters = list((ROOT / "platforms").rglob("time_adapter.c"))
        self.assertEqual(
            [ROOT / "platforms" / "k210" / "capabilities" / "time_adapter.c"],
            adapters,
        )


if __name__ == "__main__":
    unittest.main()
