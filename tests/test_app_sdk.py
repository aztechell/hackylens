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

    def test_removed_host_fake_is_not_part_of_the_public_sdk(self) -> None:
        self.assertFalse(
            (ROOT / "sdk/include/hackylens/app/host_fake.h").exists()
        )
        self.assertFalse((ROOT / "sdk/host").exists())

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

    def test_v2_cpp_app_accepts_bounded_standard_headers_only(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-cpp-boundary-") as temp:
            app = Path(temp) / "sample"
            app.mkdir(parents=True)
            source = app / "sample.cpp"
            source.write_text(
                "#include <hackylens/app.h>\n"
                "#include <array>\n"
                "#include <cstdint>\n",
                encoding="utf-8",
            )
            self.assertEqual(
                check_app_sdk.app_source_boundary_failures(app, [source], [app]),
                [],
            )
            source.write_text(
                "#include <hackylens/app.h>\n"
                "#include <boost/asio.hpp>\n",
                encoding="utf-8",
            )
            failures = check_app_sdk.app_source_boundary_failures(
                app, [source], [app]
            )
        self.assertTrue(any("escapes its private headers" in item for item in failures))

    def test_sdk_build_metadata_is_committed(self) -> None:
        cmake = (ROOT / "sdk/CMakeLists.txt").read_text(encoding="utf-8")
        make = (ROOT / "sdk/hackylens-app-sdk.mk").read_text(encoding="utf-8")
        public = (ROOT / "sdk/include/hackylens/app.h").read_text(
            encoding="utf-8"
        )
        runtime = (ROOT / "sdk/include/hackylens/app/runtime.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("HackyLens::AppSDK", cmake)
        self.assertNotIn("HackyLens::AppHostFake", cmake)
        self.assertNotIn("HACKYLENS_APP_HOST_FAKE_SOURCES", make)
        self.assertNotIn("host_fake", cmake)
        self.assertIn("HK_APP_SDK_VERSION_MINOR 2U", public)
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
