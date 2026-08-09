#!/usr/bin/env python3
"""Build HackyLens firmware through Kendryte standalone SDK."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from gen_flash_layout import load_validated, partition_by_name

WORKSPACE = ROOT.parent
LOCAL_DEPS = ROOT / "_deps"
LEGACY_DEPS = WORKSPACE / "hackylens-legacy" / "_deps"
_FLASH_LAYOUT, _FLASH_PARTITIONS = load_validated(
    ROOT / "firmware" / "config" / "flash_layout.json"
)
_FIRMWARE_PARTITION = partition_by_name(_FLASH_PARTITIONS, "firmware")
FIRMWARE_FLASH_LIMIT = (
    _FIRMWARE_PARTITION["offset"] + _FIRMWARE_PARTITION["size"]
)
K210_IMAGE_OVERHEAD = 37
FLASH_ERASE_SIZE = _FLASH_LAYOUT["erase_size"]
MICROPYTHON_NATIVE_POLL_PATCH = (
    ROOT / "firmware" / "third_party" / "micropython" /
    "patches" / "0001-poll-native-iterators.patch"
)
SEMVER_RE = re.compile(
    r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?"
)

APP_MODULES = {
    "terminal": "HK_ENABLE_APP_TERMINAL",
    "camera": "HK_ENABLE_APP_CAMERA",
    "qr-camera": "HK_ENABLE_APP_QR_CAMERA",
    "face-detect": "HK_ENABLE_APP_FACE_DETECT",
    "apriltag": "HK_ENABLE_APP_APRILTAG",
    "object-detect": "HK_ENABLE_APP_OBJECT_DETECT",
    "files": "HK_ENABLE_APP_FILES",
    "buttons": "HK_ENABLE_APP_BUTTONS",
    "pong": "HK_ENABLE_APP_PONG",
    "settings": "HK_ENABLE_APP_SETTINGS",
    "sleep": "HK_ENABLE_APP_SLEEP",
    "micropython": "HK_ENABLE_APP_MICROPYTHON",
}

APP_SOURCE_DIRS = {
    "terminal": Path("firmware/src/apps/terminal"),
    "camera": Path("firmware/src/apps/camera"),
    "qr-camera": Path("firmware/src/apps/qr_camera"),
    "face-detect": Path("firmware/src/apps/face_detect"),
    "apriltag": Path("firmware/src/apps/apriltag"),
    "object-detect": Path("firmware/src/apps/object_detect"),
    "files": Path("firmware/src/apps/files"),
    "buttons": Path("firmware/src/apps/buttons"),
    "pong": Path("firmware/src/apps/pong"),
    "settings": Path("firmware/src/apps/settings"),
    "sleep": Path("firmware/src/apps/sleep"),
    "micropython": Path("firmware/src/apps/micropython"),
}

CAMERA_FEATURE_SOURCE_MODULES = {
    Path("firmware/src/controllers/camera_runtime_controller.c"),
    Path("firmware/src/controllers/camera_runtime_controller.h"),
    Path("firmware/src/controllers/debug_camera_controller.c"),
    Path("firmware/src/controllers/debug_camera_controller.h"),
    Path("firmware/src/drivers/camera_stream.c"),
    Path("firmware/src/drivers/camera_stream.h"),
    Path("firmware/src/drivers/ov2640_sensor.c"),
    Path("firmware/src/drivers/ov2640_sensor.h"),
    Path("firmware/src/hal/hal_dvp.c"),
    Path("firmware/src/hal/hal_dvp.h"),
}
for camera_path in (ROOT / "firmware" / "src" / "services").glob("camera_*"):
    CAMERA_FEATURE_SOURCE_MODULES.add(camera_path.relative_to(ROOT))
for camera_path in (ROOT / "firmware" / "src" / "services" / "internal").glob("camera_*"):
    CAMERA_FEATURE_SOURCE_MODULES.add(camera_path.relative_to(ROOT))
for camera_path in (ROOT / "firmware" / "src" / "ui").glob("camera_*"):
    CAMERA_FEATURE_SOURCE_MODULES.add(camera_path.relative_to(ROOT))

CAMERA_AI_INPUT_SOURCE_MODULES = {
    Path("firmware/src/services/camera_ai_input.c"),
    Path("firmware/src/services/camera_ai_input.h"),
}

CORE1_EXECUTOR_SOURCE_MODULES = {
    Path("firmware/src/services/core1_executor.c"),
    Path("firmware/src/services/core1_executor.h"),
}

MICROPYTHON_FEATURE_SOURCE_MODULES = {
    Path("firmware/src/hal/hal_watchdog.c"),
    Path("firmware/src/hal/hal_watchdog.h"),
    Path("firmware/src/services/hmpy_codec.c"),
    Path("firmware/src/services/hmpy_codec.h"),
    Path("firmware/src/services/hmpy_session.c"),
    Path("firmware/src/services/hmpy_session.h"),
    Path("firmware/src/services/micropython_program.c"),
    Path("firmware/src/services/micropython_program.h"),
    Path("firmware/src/services/micropython_binding_service.c"),
    Path("firmware/src/services/micropython_binding_service.h"),
    Path("firmware/src/services/micropython_port.c"),
    Path("firmware/src/services/micropython_runtime.c"),
    Path("firmware/src/services/micropython_runtime.h"),
    Path("firmware/src/storage/userfs.c"),
    Path("firmware/src/storage/userfs.h"),
}

TARGETS = {
    "full": {
        "project": "hackylens_full",
        "output": "hackylens.bin",
        "build_dir": "sdk-full",
        "target_source": ROOT / "firmware" / "targets" / "full.c",
    },
}


def dep_roots() -> list[Path]:
    return [LOCAL_DEPS, LEGACY_DEPS]


def find_sdk() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("KENDRYTE_SDK_DIR"):
        candidates.append(Path(os.environ["KENDRYTE_SDK_DIR"]))
    for root in dep_roots():
        candidates.append(root / "kendryte-standalone-sdk")
    for path in candidates:
        if (path / "CMakeLists.txt").is_file() and (path / "lib").is_dir():
            return path.resolve()
    return None


def find_toolchain_bin() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("KENDRYTE_TOOLCHAIN_BIN"):
        candidates.append(Path(os.environ["KENDRYTE_TOOLCHAIN_BIN"]))
    for name in ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"):
        exe = shutil.which(name)
        if exe:
            candidates.append(Path(exe).parent)
    for root in dep_roots():
        if root.is_dir():
            for name in ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"):
                candidates.extend(path.parent for path in root.rglob(name) if path.is_file())
    for path in candidates:
        for exe_name in ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"):
            if (path / exe_name).is_file():
                return path.resolve()
    return None


def find_micropython() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("HACKYLENS_MICROPYTHON_DIR"):
        candidates.append(Path(os.environ["HACKYLENS_MICROPYTHON_DIR"]))
    for root in dep_roots():
        candidates.append(root / "micropython")
    for path in candidates:
        if (path / "ports" / "embed" / "embed.mk").is_file():
            return path.resolve()
    return None


def find_littlefs() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("HACKYLENS_LITTLEFS_DIR"):
        candidates.append(Path(os.environ["HACKYLENS_LITTLEFS_DIR"]))
    for root in dep_roots():
        candidates.append(root / "littlefs")
    for path in candidates:
        if all((path / name).is_file() for name in
               ("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h")):
            return path.resolve()
    return None


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+ " + " ".join(str(part) for part in cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def read_firmware_version(path: Path | None = None) -> str:
    version_path = path if path is not None else ROOT / "VERSION"
    version = version_path.read_text(encoding="utf-8").strip()
    if not SEMVER_RE.fullmatch(version):
        raise RuntimeError(f"invalid semantic version in {version_path}: {version!r}")
    return version


def cmake_path(path: Path) -> str:
    return str(path).replace("\\", "/")


def find_git_bash() -> tuple[Path, Path] | None:
    git = shutil.which("git")
    if not git:
        return None
    git_path = Path(git).resolve()
    for root in (git_path.parent.parent, git_path.parent):
        bash = root / "bin" / "bash.exe"
        usr_bin = root / "usr" / "bin"
        if bash.is_file() and usr_bin.is_dir():
            return bash, usr_bin
    return None


def apply_micropython_native_poll_patch(package: Path) -> None:
    git = shutil.which("git")
    if not git:
        raise RuntimeError("git is required to apply the pinned MicroPython port patch")
    if not MICROPYTHON_NATIVE_POLL_PATCH.is_file():
        raise RuntimeError(
            f"MicroPython native-poll patch not found: {MICROPYTHON_NATIVE_POLL_PATCH}"
        )
    runtime_source = package / "py" / "runtime.c"
    if not runtime_source.is_file():
        raise RuntimeError(f"MicroPython runtime source not found: {runtime_source}")

    try:
        package_relative = package.resolve().relative_to(ROOT.resolve())
    except ValueError:
        apply_cwd = package
        directory_arg: list[str] = []
    else:
        apply_cwd = ROOT
        directory_arg = [f"--directory={cmake_path(package_relative)}"]
    base_cmd = [git, "apply", *directory_arg]
    run([*base_cmd, "--check", str(MICROPYTHON_NATIVE_POLL_PATCH)], cwd=apply_cwd)
    run([*base_cmd, str(MICROPYTHON_NATIVE_POLL_PATCH)], cwd=apply_cwd)
    patched = runtime_source.read_text(encoding="utf-8")
    if patched.count("MICROPY_PORT_ITERNEXT_HOOK") != 2:
        raise RuntimeError("MicroPython native iterator poll patch is incomplete")


def generate_micropython_embed(micropython: Path) -> Path:
    config_dir = ROOT / "firmware" / "third_party" / "micropython"
    makefile = config_dir / "micropython_embed.mk"
    work = ROOT / "build" / "micropython-embed-work"
    package = ROOT / "build" / "micropython_embed"
    make = shutil.which("mingw32-make") if os.name == "nt" else shutil.which("make")
    if not make:
        raise RuntimeError("GNU make is required to generate the MicroPython embed package")

    env = os.environ.copy()
    makefile_arg = cmake_path(Path(os.path.relpath(makefile, config_dir)))
    micropython_arg = cmake_path(Path(os.path.relpath(micropython, config_dir)))
    work_arg = cmake_path(Path(os.path.relpath(work, config_dir)))
    package_arg = cmake_path(Path(os.path.relpath(package, config_dir)))
    cmd = [
        make,
        "-f", makefile_arg,
        f"MICROPYTHON_TOP={micropython_arg}",
        f"BUILD={work_arg}",
        f"PACKAGE_DIR={package_arg}",
        f"PYTHON={cmake_path(Path(sys.executable))}",
    ]
    if os.name == "nt":
        git_bash = find_git_bash()
        if not git_bash:
            raise RuntimeError("Git for Windows bash/coreutils are required to generate MicroPython")
        bash, usr_bin = git_bash
        env["PATH"] = str(usr_bin) + os.pathsep + env.get("PATH", "")
        cmd.append(f"SHELL={cmake_path(bash)}")

    print(f"[GENERATE] MicroPython embed from {micropython}")
    print("+ " + " ".join(str(part) for part in cmd))
    subprocess.run(cmd, cwd=config_dir, env=env, check=True)
    required = (
        package / "genhdr" / "qstrdefs.generated.h",
        package / "genhdr" / "moduledefs.h",
        package / "port" / "micropython_embed.h",
    )
    if any(not path.is_file() for path in required):
        raise RuntimeError(f"MicroPython embed generator produced an incomplete package: {package}")
    apply_micropython_native_poll_patch(package)
    return package


def write_micropython_project_cmake(stage: Path, package: Path) -> None:
    sources = [
        path for path in sorted(package.rglob("*.c"))
        if path.relative_to(package).as_posix() != "port/mphalport.c"
    ]
    if not sources:
        raise RuntimeError(f"MicroPython embed package contains no C sources: {package}")
    lines = ["# Generated by tools/build_firmware.py", "set(HACKYLENS_MICROPYTHON_SOURCES"]
    lines.extend(f'    "{cmake_path(path)}"' for path in sources)
    lines.extend([
        ")",
        "target_sources(${PROJECT_NAME} PRIVATE ${HACKYLENS_MICROPYTHON_SOURCES})",
        "target_include_directories(${PROJECT_NAME} PRIVATE",
        f'    "{cmake_path(ROOT / "firmware" / "third_party" / "micropython")}"',
        f'    "{cmake_path(package)}"',
        ")",
        "target_compile_definitions(${PROJECT_NAME} PRIVATE",
        "    LFS_NO_MALLOC",
        "    LFS_NAME_MAX=63",
        ")",
        "",
    ])
    (stage / "project.cmake").write_text("\n".join(lines), encoding="utf-8")


def copy_tree_files(src: Path, dst: Path) -> None:
    if not src.is_dir():
        return
    for path in src.rglob("*"):
        if path.is_file():
            rel = path.relative_to(src)
            out = dst / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, out)


def stage_firmware_sources(stage: Path, disabled_apps: set[str]) -> None:
    disabled_dirs = {APP_SOURCE_DIRS[app] for app in disabled_apps}
    camera_feature_enabled = ("camera" not in disabled_apps or
                              "qr-camera" not in disabled_apps or
                              "face-detect" not in disabled_apps or
                              "apriltag" not in disabled_apps or
                              "object-detect" not in disabled_apps)
    camera_ai_input_enabled = ("face-detect" not in disabled_apps or
                               "object-detect" not in disabled_apps)
    core1_executor_enabled = ("apriltag" not in disabled_apps or
                              "micropython" not in disabled_apps)
    micropython_feature_enabled = "micropython" not in disabled_apps

    for path in (ROOT / "firmware" / "src").rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if rel.suffix == ".inc":
            continue
        if any(rel.is_relative_to(disabled_dir) for disabled_dir in disabled_dirs):
            print(f"[SKIP] disabled app source {rel}")
            continue
        if not camera_ai_input_enabled and rel in CAMERA_AI_INPUT_SOURCE_MODULES:
            print(f"[SKIP] unused camera AI input source {rel}")
            continue
        if not core1_executor_enabled and rel in CORE1_EXECUTOR_SOURCE_MODULES:
            print(f"[SKIP] unused core-1 executor source {rel}")
            continue
        if (not micropython_feature_enabled and
                rel in MICROPYTHON_FEATURE_SOURCE_MODULES):
            print(f"[SKIP] unused MicroPython source {rel}")
            continue
        if not camera_feature_enabled and rel in CAMERA_FEATURE_SOURCE_MODULES:
            print(f"[SKIP] unused camera source {rel}")
            continue
        out = stage / path.relative_to(ROOT / "firmware" / "src")
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, out)


def write_config(stage: Path, disabled_apps: set[str],
                 wdt_fault_injection: bool = False) -> None:
    version = read_firmware_version()
    if wdt_fault_injection:
        version += "-wdtfi"
    lines = [
        "#ifndef HK_CONFIG_H",
        "#define HK_CONFIG_H",
        f'#define HACKYLENS_VERSION "{version}"',
    ]
    for app, flag in APP_MODULES.items():
        lines.append(f"#define {flag} {0 if app in disabled_apps else 1}")
    lines.extend([
        f"#define HK_MICROPYTHON_WDT_FAULT_INJECTION {1 if wdt_fault_injection else 0}",
        "#define HK_ENABLE_CAMERA_FEATURE (HK_ENABLE_APP_CAMERA || HK_ENABLE_APP_QR_CAMERA || HK_ENABLE_APP_FACE_DETECT || HK_ENABLE_APP_APRILTAG || HK_ENABLE_APP_OBJECT_DETECT)",
        "#define HK_ENABLE_QR_FEATURE HK_ENABLE_APP_QR_CAMERA",
        '#include "hk_config_default.h"',
        "#endif",
        "",
    ])
    (stage / "hk_config.h").write_text("\n".join(lines), encoding="utf-8")


def stage_target(sdk: Path, target_name: str, disabled_apps: set[str],
                  micropython_package: Path | None = None,
                  littlefs: Path | None = None,
                  wdt_fault_injection: bool = False) -> Path:
    target = TARGETS[target_name]
    stage = sdk / "src" / str(target["project"])
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True, exist_ok=True)

    shutil.copy2(Path(target["target_source"]), stage / "main.c")
    stage_firmware_sources(stage, disabled_apps)

    for header in (ROOT / "firmware" / "assets").glob("*.h"):
        shutil.copy2(header, stage / header.name)
    shutil.copy2(ROOT / "firmware" / "config" / "hk_config_default.h", stage / "hk_config_default.h")

    if "qr-camera" not in disabled_apps:
        quirc = ROOT / "firmware" / "third_party" / "quirc"
        for source in quirc.glob("*.c"):
            shutil.copy2(source, stage / source.name)
        for header in quirc.glob("*.h"):
            shutil.copy2(header, stage / header.name)

    if "apriltag" not in disabled_apps:
        copy_tree_files(ROOT / "firmware" / "third_party" / "apriltag", stage)

    if littlefs is not None:
        for name in ("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"):
            shutil.copy2(littlefs / name, stage / name)

    write_config(stage, disabled_apps, wdt_fault_injection)
    if micropython_package is not None:
        write_micropython_project_cmake(stage, micropython_package)
    return stage


def build_target(name: str, sdk: Path, toolchain_bin: Path,
                 disabled_apps: set[str],
                 wdt_fault_injection: bool = False) -> Path:
    target = TARGETS[name]
    project = str(target["project"])
    output_name = Path(str(target["output"]))
    if wdt_fault_injection:
        output_name = output_name.with_name(
            f"{output_name.stem}-wdtfi{output_name.suffix}"
        )
        print("[DANGER] building non-release WDT fault-injection firmware")
    out_image = ROOT / "build" / output_name
    micropython_package = None
    littlefs = None
    if "micropython" not in disabled_apps:
        micropython = find_micropython()
        if not micropython:
            raise RuntimeError(
                "pinned MicroPython checkout not found; run python tools\\bootstrap_deps.py"
            )
        micropython_package = generate_micropython_embed(micropython)
        littlefs = find_littlefs()
        if not littlefs:
            raise RuntimeError(
                "pinned littlefs checkout not found; run python tools\\bootstrap_deps.py"
            )
    stage = stage_target(
        sdk, name, disabled_apps, micropython_package, littlefs,
        wdt_fault_injection,
    )
    print(f"[STAGE] {stage}")

    build_dir = ROOT / "build" / str(target["build_dir"])
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    generator = "MinGW Makefiles" if os.name == "nt" else "Ninja"
    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("cmake not found")

    run([
        cmake,
        "-S",
        cmake_path(sdk),
        "-B",
        cmake_path(build_dir),
        "-G",
        generator,
        f"-DPROJ={project}",
        f"-DTOOLCHAIN={cmake_path(toolchain_bin)}",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    ])
    run([cmake, "--build", cmake_path(build_dir), "--parallel"])

    built = build_dir / f"{project}.bin"
    if not built.is_file() or built.stat().st_size == 0:
        raise RuntimeError(f"build did not produce a non-empty image: {built}")
    flashed_size = ((built.stat().st_size + K210_IMAGE_OVERHEAD + FLASH_ERASE_SIZE - 1)
                    // FLASH_ERASE_SIZE * FLASH_ERASE_SIZE)
    if flashed_size > FIRMWARE_FLASH_LIMIT:
        raise RuntimeError(
            f"firmware image exceeds canonical firmware partition "
            f"0x{FIRMWARE_FLASH_LIMIT:06X}: "
            f"raw={built.stat().st_size} flashed={flashed_size} bytes"
        )

    out_image.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built, out_image)
    if wdt_fault_injection:
        print("[DANGER] isolated test image kept under build/; dist/ was not touched")
    else:
        run([sys.executable, str(ROOT / "tools" / "make_image.py"),
             str(out_image), "--out-dir", str(ROOT / "dist")])
    print(f"[OK] {out_image} ({out_image.stat().st_size} bytes)")
    return out_image


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build HackyLens full firmware")
    parser.add_argument("target", choices=sorted(TARGETS))
    parser.add_argument(
        "--disable-app",
        action="append",
        default=[],
        choices=sorted(APP_MODULES),
        help="Disable an app in the menu registry. Can be repeated.",
    )
    parser.add_argument(
        "--wdt-fault-injection",
        action="store_true",
        help=(
            "DANGEROUS TEST BUILD: wedge core 1 on MicroPython STOP/deadline "
            "to exercise the WDT1 recovery path"
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    sdk = find_sdk()
    if not sdk:
        print("[ERR] Kendryte SDK not found. Run: python hackylens\\tools\\bootstrap_deps.py", file=sys.stderr)
        return 1
    toolchain = find_toolchain_bin()
    if not toolchain:
        print("[ERR] Kendryte toolchain not found. Run python tools\\bootstrap_deps.py and . .\\env.ps1", file=sys.stderr)
        return 1

    disabled_apps = set(args.disable_app)
    if args.wdt_fault_injection and "micropython" in disabled_apps:
        raise RuntimeError(
            "--wdt-fault-injection requires the micropython app"
        )
    build_target(
        args.target, sdk, toolchain, disabled_apps,
        args.wdt_fault_injection,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except subprocess.CalledProcessError as exc:
        print(f"[ERR] command failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
    except Exception as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        raise SystemExit(1)
