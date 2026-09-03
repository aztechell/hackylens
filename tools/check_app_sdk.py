#!/usr/bin/env python3
"""Validate and build the standalone Feature App SDK."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib


ROOT = Path(__file__).resolve().parents[1]
SDK_INCLUDE = ROOT / "sdk" / "include"
CAPABILITY_INCLUDE = ROOT / "firmware" / "include" / "hackylens" / "capability"
FIXTURE = ROOT / "tests" / "fixtures" / "app_sdk"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*(?:<(?P<angle>[^>]+)>|"(?P<quote>[^"]+)")\s*$',
    re.MULTILINE,
)
SYSTEM_HEADERS = {
    "assert.h", "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "string.h", "limits.h", "inttypes.h", "float.h", "math.h", "time.h",
    "algorithm", "array", "bitset", "cassert", "cctype", "cerrno", "cfloat",
    "cinttypes", "climits", "cmath", "cstddef", "cstdint", "cstdio",
    "cstdlib", "cstring", "ctime", "initializer_list", "limits", "optional",
    "ratio", "string_view", "tuple", "type_traits", "utility", "variant",
}
FORBIDDEN_TOKENS = (
    "firmware/src/", "platforms/", "boards/", "runtime_private.h",
    "capability_provider.h", "provider_vtable", "screen_t", "hal_", "driver/",
    "drivers/", "raw_sd", "framebuffer_owner",
)


def include_names(path: Path) -> list[str]:
    source = path.read_text(encoding="utf-8")
    return [
        match.group("angle") or match.group("quote")
        for match in INCLUDE_RE.finditer(source)
    ]


def public_header_closure_failures(
    sdk_include: Path = SDK_INCLUDE,
    capability_include: Path = CAPABILITY_INCLUDE,
) -> list[str]:
    failures: list[str] = []
    headers = sorted(
        path for path in sdk_include.rglob("*")
        if path.is_file() and path.suffix.casefold() in {".h", ".hpp"}
    )
    if not headers:
        return [f"{sdk_include}: public SDK contains no headers"]
    for path in headers:
        relative = path.relative_to(sdk_include).as_posix()
        text = path.read_text(encoding="utf-8")
        for token in FORBIDDEN_TOKENS:
            if token.casefold() in text.casefold():
                failures.append(f"{relative}: forbidden private token {token!r}")
        for include in include_names(path):
            normalized = include.replace("\\", "/")
            if normalized in SYSTEM_HEADERS:
                continue
            candidates: list[Path] = [path.parent / normalized]
            if normalized.startswith("hackylens/"):
                candidates.extend((
                    sdk_include / normalized,
                    capability_include.parent.parent / normalized,
                ))
            resolved = next(
                (candidate.resolve() for candidate in candidates if candidate.is_file()),
                None,
            )
            if resolved is None:
                failures.append(f"{relative}: unresolved public include {include!r}")
                continue
            try:
                resolved.relative_to(sdk_include.resolve())
                continue
            except ValueError:
                pass
            try:
                resolved.relative_to(capability_include.resolve())
            except ValueError:
                failures.append(
                    f"{relative}: public closure reaches non-capability header "
                    f"{resolved.as_posix()}"
                )
    return sorted(set(failures))


def app_source_boundary_failures(
    app_root: Path,
    production_sources: list[Path],
    private_include_roots: list[Path],
) -> list[str]:
    failures: list[str] = []
    owned_roots = [app_root.resolve(), *(path.resolve() for path in private_include_roots)]
    for path in sorted(set(production_sources)):
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(app_root).as_posix()
        for include in include_names(path):
            normalized = include.replace("\\", "/")
            if normalized in SYSTEM_HEADERS or normalized.startswith("hackylens/app"):
                continue
            if normalized.startswith("hackylens/"):
                failures.append(
                    f"{relative}: lifecycle-v2 app must include the App SDK, "
                    f"not {include!r} directly"
                )
                continue
            candidates = [path.parent / normalized]
            candidates.extend(root / normalized for root in private_include_roots)
            resolved = next(
                (candidate.resolve() for candidate in candidates if candidate.is_file()),
                None,
            )
            if resolved is None or not any(
                resolved == root or root in resolved.parents for root in owned_roots
            ):
                failures.append(
                    f"{relative}: lifecycle-v2 app include escapes its private "
                    f"headers: {include!r}"
                )
    return sorted(set(failures))


def production_v2_app_failures(
    apps_root: Path = ROOT / "firmware" / "src" / "apps",
) -> list[str]:
    failures: list[str] = []
    for manifest_path in sorted(apps_root.glob("*/app.toml")):
        manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("lifecycle") != "v2":
            continue
        app_root = manifest_path.parent
        sources = [app_root / value for value in manifest.get("sources", [])]
        private_roots = [
            app_root / value for value in manifest.get("private_includes", [])
        ]
        headers = [
            path for root in private_roots for path in root.rglob("*")
            if path.is_file() and path.suffix.casefold() in SOURCE_SUFFIXES
        ]
        failures.extend(app_source_boundary_failures(
            app_root, sources + headers, private_roots
        ))
    return sorted(set(failures))


def source_boundary_failures() -> list[str]:
    failures = public_header_closure_failures()
    failures.extend(production_v2_app_failures())
    fixture_sources = [FIXTURE / "minimal_app.c", FIXTURE / "minimal_private.h"]
    failures.extend(app_source_boundary_failures(FIXTURE, fixture_sources, [FIXTURE]))
    return sorted(set(failures))


def compiler(language: str) -> str:
    variable = "CXX" if language == "c++" else "CC"
    candidates = (
        os.environ.get(variable),
        shutil.which("g++" if language == "c++" else "gcc"),
        shutil.which("c++" if language == "c++" else "cc"),
    )
    value = next((candidate for candidate in candidates if candidate), None)
    if value is None:
        raise RuntimeError(f"host {language} compiler is required")
    return str(value)


def run_executable(path: Path) -> None:
    result = subprocess.run(
        [str(path)], cwd=path.parent, check=True, capture_output=True, text=True,
        timeout=30,
    )
    if result.stdout != "APP_SDK_FIXTURE_OK\n":
        raise RuntimeError(f"unexpected SDK fixture output: {result.stdout!r}")


def compile_header_consumers(temp: Path) -> None:
    source = """#include <hackylens/app.h>
#ifdef __cplusplus
static_assert(HK_APP_SDK_VERSION_MINOR == 1U);
static_assert(HK_APP_STATE_ALIGNMENT == 16U);
int main(void) { hk_app_v2_entry_t entry{}; return entry.probe != nullptr; }
#else
_Static_assert(HK_APP_SDK_VERSION_MINOR == 1U, "sdk version");
_Static_assert(HK_APP_STATE_ALIGNMENT == 16U, "state alignment");
int main(void) { hk_app_v2_entry_t entry = {0}; return entry.probe != 0; }
#endif
"""
    for language, suffix, standard in (("c", "c", "c11"), ("c++", "cpp", "c++17")):
        path = temp / f"consumer.{suffix}"
        output = temp / f"consumer-{suffix}.o"
        path.write_text(source, encoding="utf-8")
        subprocess.run([
            compiler(language), f"-std={standard}", "-Wall", "-Wextra", "-Werror",
            f"-I{SDK_INCLUDE}", f"-I{CAPABILITY_INCLUDE.parent.parent}",
            "-c", str(path), "-o", str(output),
        ], cwd=ROOT, check=True)


def build_standalone_fixture() -> None:
    cmake = shutil.which("cmake")
    ninja = shutil.which("ninja")
    make = shutil.which("mingw32-make") or shutil.which("make")
    if not cmake or not ninja or not make:
        raise RuntimeError("cmake, ninja, and make are required for SDK conformance")
    with tempfile.TemporaryDirectory(prefix="hackylens-app-sdk-") as directory:
        temp = Path(directory)
        source = temp / "fixture"
        shutil.copytree(FIXTURE, source)
        cmake_build = temp / "cmake-build"
        subprocess.run([
            cmake, "-S", str(source), "-B", str(cmake_build), "-G", "Ninja",
            # Native separators are intentional: CMake must accept Windows paths
            # such as D:\a\... without treating \a as an escape.
            f"-DHACKYLENS_SOURCE_DIR={os.fspath(ROOT)}",
            f"-DCMAKE_C_COMPILER={compiler('c')}",
        ], cwd=ROOT, check=True)
        subprocess.run([cmake, "--build", str(cmake_build)], cwd=ROOT, check=True)
        suffix = ".exe" if os.name == "nt" else ""
        run_executable(cmake_build / f"app_sdk_fixture{suffix}")

        make_source = temp / "make-fixture"
        shutil.copytree(FIXTURE, make_source)
        environment = os.environ.copy()
        environment["HACKYLENS_SOURCE_DIR"] = ROOT.as_posix()
        environment["CC"] = compiler("c")
        subprocess.run([make, "all"], cwd=make_source, env=environment, check=True)
        run_executable(make_source / "build-make" / f"app_sdk_fixture{suffix}")
        compile_header_consumers(temp)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-only", action="store_true",
        help="check dependency closure without compiling standalone fixtures",
    )
    args = parser.parse_args(argv)
    failures = source_boundary_failures()
    if failures:
        print("[APP SDK] boundary violations:")
        for failure in failures:
            print("  " + failure)
        return 1
    if not args.source_only:
        build_standalone_fixture()
    print("[OK] Feature App SDK closure and standalone runtime fixture passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
