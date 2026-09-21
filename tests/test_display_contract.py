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


class DisplayContractTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_fake_proves_display_state_machine(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-display-") as temp:
            executable = Path(temp) / (
                "display_contract.exe" if os.name == "nt" else "display_contract"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'tests'}",
                str(ROOT / "tests" / "display_contract_harness.c"),
                str(ROOT / "tests" / "display_normative_suite.c"),
                str(ROOT / "tests" / "capability_fake_display.c"),
                str(ROOT / "firmware" / "src" / "capabilities" / "display.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        self.assertIn(
            "DISPLAY_CONTRACT_OK cases=16 normative=8 full_bytes=384 slice_bytes=8",
            result.stdout,
        )

    def test_public_abi_and_fake_are_fixed_capacity(self) -> None:
        compiler = self.compiler()
        source = """
            #include <hackylens/capability/display.h>
            #include <stddef.h>
            _Static_assert(sizeof(hk_display_t) <= 2 * sizeof(void *),
                           "display session must remain compact");
            _Static_assert(HK_CAPABILITY_ID_DISPLAY == 0x00010003U,
                           "display capability ID changed");
            _Static_assert(HK_DISPLAY_FORMAT_RGB565_BE == 1U,
                           "RGB565 byte format changed");
            int main(void) {
                hk_display_t session = {0};
                return session.service == NULL ? 0 : 1;
            }
        """
        with tempfile.TemporaryDirectory(prefix="hackylens-display-abi-") as temp:
            temporary = Path(temp)
            translation_unit = temporary / "abi.c"
            executable = temporary / (
                "abi.exe" if os.name == "nt" else "abi"
            )
            fake_object = temporary / "fake.o"
            translation_unit.write_text(source, encoding="utf-8")
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'tests'}",
            ]
            subprocess.run([
                compiler, *common, str(translation_unit), "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)
            subprocess.run([
                compiler, *common, "-c",
                str(ROOT / "tests" / "capability_fake_display.c"),
                "-o", str(fake_object),
            ], check=True, cwd=ROOT)
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(fake_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in (
                    "malloc", "calloc", "realloc", "free", "task", "queue"
                ):
                    self.assertNotIn(forbidden, symbols)

    def test_production_apps_require_display_binding(self) -> None:
        for app in app_composition.load_model()["apps"]:
            self.assertIn("display", app["requires"], app["id"])
        self.assertTrue((ROOT / "platforms/k210/capabilities/display_adapter.c").is_file())


if __name__ == "__main__":
    unittest.main()
