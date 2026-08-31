from __future__ import annotations

from pathlib import Path
import shutil
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import check_app_sdk


class FeatureAppSdkTests(unittest.TestCase):
    def test_repository_sdk_closure_and_v2_fixture_are_clean(self) -> None:
        self.assertEqual(check_app_sdk.source_boundary_failures(), [])

    def test_private_header_injected_into_sdk_closure_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-sdk-negative-") as temp:
            include = Path(temp) / "include"
            header = include / "hackylens" / "app.h"
            header.parent.mkdir(parents=True)
            header.write_text(
                "#include <firmware/src/app_runtime/runtime_private.h>\n",
                encoding="utf-8",
            )
            failures = check_app_sdk.public_header_closure_failures(include)
        self.assertTrue(any("forbidden private token" in item for item in failures))

    def test_host_fake_private_provider_dependency_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-fake-negative-") as temp:
            host = Path(temp) / "host"
            host.mkdir()
            (host / "fake.c").write_text(
                '#include "capability_provider.h"\n', encoding="utf-8"
            )
            failures = check_app_sdk.host_fake_source_failures(host)
        self.assertTrue(any("private token" in item for item in failures))

    def test_v2_app_direct_capability_and_foreign_private_include_are_rejected(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-app-negative-") as temp:
            app = Path(temp) / "sample"
            private = app / "private"
            private.mkdir(parents=True)
            source = app / "sample.c"
            source.write_text(
                "#include <hackylens/capability/time.h>\n"
                '#include "../other/private.h"\n',
                encoding="utf-8",
            )
            failures = check_app_sdk.app_source_boundary_failures(
                app, [source], [private]
            )
        self.assertTrue(any("must include the App SDK" in item for item in failures))
        self.assertTrue(any("escapes its private headers" in item for item in failures))

    def test_sdk_build_metadata_and_ci_gate_are_committed(self) -> None:
        cmake = (ROOT / "sdk/CMakeLists.txt").read_text(encoding="utf-8")
        make = (ROOT / "sdk/hackylens-app-sdk.mk").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        public = (ROOT / "sdk/include/hackylens/app.h").read_text(
            encoding="utf-8"
        )
        runtime = (ROOT / "sdk/include/hackylens/app/runtime.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("HackyLens::AppSDK", cmake)
        self.assertIn("HackyLens::AppHostFake", cmake)
        self.assertIn("HACKYLENS_APP_HOST_FAKE_SOURCES", make)
        self.assertIn("python tools/check_app_sdk.py", workflow)
        self.assertIn("HK_APP_SDK_VERSION_MINOR 1U", public)
        self.assertIn("HK_APP_SDK_MANIFEST_SCHEMA_MAJOR 1U", public)
        self.assertIn("typedef struct hk_app_v2_entry", runtime)

    @unittest.skipUnless(
        shutil.which("cmake") and shutil.which("ninja") and
        (shutil.which("mingw32-make") or shutil.which("make")),
        "CMake, Ninja, and Make are required",
    )
    def test_standalone_cmake_make_and_c_cpp_consumers(self) -> None:
        check_app_sdk.build_standalone_fixture()


if __name__ == "__main__":
    unittest.main()
