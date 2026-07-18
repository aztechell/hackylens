#!/usr/bin/env python3
"""Build HackyLens 0.1.0 full firmware through Kendryte standalone SDK."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
LOCAL_DEPS = ROOT / "_deps"
LEGACY_DEPS = WORKSPACE / "hackylens-legacy" / "_deps"
SETTINGS_FLASH_OFFSET = 0x007FE000

APP_MODULES = {
    "terminal": "HK_ENABLE_APP_TERMINAL",
    "camera": "HK_ENABLE_APP_CAMERA",
    "qr-camera": "HK_ENABLE_APP_QR_CAMERA",
    "face-detect": "HK_ENABLE_APP_FACE_DETECT",
    "files": "HK_ENABLE_APP_FILES",
    "buttons": "HK_ENABLE_APP_BUTTONS",
    "pong": "HK_ENABLE_APP_PONG",
    "settings": "HK_ENABLE_APP_SETTINGS",
    "sleep": "HK_ENABLE_APP_SLEEP",
}

APP_SOURCE_MODULES = {
    "terminal": [
        Path("firmware/src/apps/terminal/terminal_app.c"),
        Path("firmware/src/apps/terminal/terminal_app.h"),
        Path("firmware/src/apps/terminal/terminal_controller.c"),
        Path("firmware/src/apps/terminal/terminal_controller.h"),
        Path("firmware/src/apps/terminal/terminal_view.c"),
        Path("firmware/src/apps/terminal/terminal_view.h"),
        Path("firmware/src/apps/terminal/terminal_buffer.c"),
        Path("firmware/src/apps/terminal/terminal_buffer.h"),
        Path("firmware/src/apps/terminal/terminal_config.h"),
        Path("firmware/src/apps/terminal/terminal_types.h"),
    ],
    "camera": [
        Path("firmware/src/apps/app_camera.c"),
    ],
    "qr-camera": [
        Path("firmware/src/apps/app_qr_camera.c"),
    ],
    "face-detect": [
        Path("firmware/src/apps/face_detect/face_detect_app.c"),
        Path("firmware/src/apps/face_detect/face_detect_app.h"),
        Path("firmware/src/apps/face_detect/face_detect_config.h"),
        Path("firmware/src/apps/face_detect/face_detect_controller.c"),
        Path("firmware/src/apps/face_detect/face_detect_controller.h"),
        Path("firmware/src/apps/face_detect/face_detect_detector.c"),
        Path("firmware/src/apps/face_detect/face_detect_detector.h"),
        Path("firmware/src/apps/face_detect/face_detect_model_storage.c"),
        Path("firmware/src/apps/face_detect/face_detect_model_storage.h"),
        Path("firmware/src/apps/face_detect/face_detect_types.h"),
        Path("firmware/src/apps/face_detect/face_detect_view.c"),
        Path("firmware/src/apps/face_detect/face_detect_view.h"),
    ],
    "buttons": [
        Path("firmware/src/apps/app_buttons.c"),
        Path("firmware/src/controllers/buttons_controller.c"),
    ],
    "pong": [
        Path("firmware/src/apps/pong/pong_app.c"),
        Path("firmware/src/apps/pong/pong_controller.c"),
        Path("firmware/src/apps/pong/pong_view.c"),
    ],
    "settings": [
        Path("firmware/src/apps/app_settings.c"),
        Path("firmware/src/controllers/settings_actions.c"),
        Path("firmware/src/controllers/settings_controller.c"),
        Path("firmware/src/controllers/settings_model.c"),
        Path("firmware/src/ui/settings_view.c"),
    ],
    "files": [
        Path("firmware/src/apps/app_files.c"),
        Path("firmware/src/controllers/files_controller.c"),
        Path("firmware/src/storage/file_browser_actions.c"),
        Path("firmware/src/storage/file_delete.c"),
        Path("firmware/src/storage/file_preview.c"),
        Path("firmware/src/storage/files_view_bridge.c"),
        Path("firmware/src/storage/image_decode_bmp.c"),
        Path("firmware/src/storage/image_decode_common.c"),
        Path("firmware/src/storage/image_decode_png.c"),
        Path("firmware/src/storage/image_decode_png_inflate.c"),
        Path("firmware/src/storage/image_decode_ppm.c"),
        Path("firmware/src/storage/image_decode_raw.c"),
        Path("firmware/src/storage/image_viewer.c"),
        Path("firmware/src/storage/photos_files.c"),
        Path("firmware/src/ui/files_view.c"),
    ],
    "sleep": [
        Path("firmware/src/apps/app_sleep.c"),
        Path("firmware/src/controllers/sleep_controller.c"),
    ],
}

CAMERA_FEATURE_SOURCE_MODULES = [
    Path("firmware/src/controllers/camera_photo_controller.c"),
    Path("firmware/src/controllers/camera_photo_mode_controller.c"),
    Path("firmware/src/controllers/camera_runtime_controller.c"),
    Path("firmware/src/controllers/camera_settings_controller.c"),
    Path("firmware/src/controllers/camera_settings_coordinator.c"),
    Path("firmware/src/controllers/camera_settings_model.c"),
    Path("firmware/src/controllers/debug_camera_controller.c"),
    Path("firmware/src/controllers/qr_result_controller.c"),
    Path("firmware/src/controllers/qr_camera_mode_controller.c"),
    Path("firmware/src/drivers/ov2640_sensor.c"),
    Path("firmware/src/drivers/camera_stream.c"),
    Path("firmware/src/hal/hal_dvp.c"),
    Path("firmware/src/services/camera_capture.c"),
    Path("firmware/src/services/camera_debug.c"),
    Path("firmware/src/services/camera_fps.c"),
    Path("firmware/src/services/camera_frame_access.c"),
    Path("firmware/src/services/camera_input_state.c"),
    Path("firmware/src/services/camera_light_state.c"),
    Path("firmware/src/services/camera_session.c"),
    Path("firmware/src/services/camera_session_state.c"),
    Path("firmware/src/services/camera_settings_state.c"),
    Path("firmware/src/services/photo_service.c"),
    Path("firmware/src/services/qr_decoder_engine.c"),
    Path("firmware/src/services/qr_camera_frame_adapter.c"),
    Path("firmware/src/services/qr_luma.c"),
    Path("firmware/src/services/qr_result_state.c"),
    Path("firmware/src/services/qr_service.c"),
    Path("firmware/src/storage/photo_path.c"),
    Path("firmware/src/storage/photo_writer.c"),
    Path("firmware/src/storage/qr_text_path.c"),
    Path("firmware/src/storage/qr_text_writer.c"),
    Path("firmware/src/ui/camera_settings_view.c"),
    Path("firmware/src/ui/camera_status_view.c"),
    Path("firmware/src/ui/camera_view.c"),
    Path("firmware/src/ui/qr_result_view.c"),
]

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


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+ " + " ".join(str(part) for part in cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def cmake_path(path: Path) -> str:
    return str(path).replace("\\", "/")


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
    disabled_sources: set[Path] = set()
    for app in disabled_apps:
        for rel in APP_SOURCE_MODULES.get(app, []):
            disabled_sources.add(rel)
    camera_feature_enabled = ("camera" not in disabled_apps or
                              "qr-camera" not in disabled_apps or
                              "face-detect" not in disabled_apps)
    if not camera_feature_enabled:
        disabled_sources.update(CAMERA_FEATURE_SOURCE_MODULES)

    for path in (ROOT / "firmware" / "src").rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if rel.suffix == ".inc":
            continue
        if rel in disabled_sources:
            print(f"[SKIP] disabled app source {rel}")
            continue
        out = stage / path.relative_to(ROOT / "firmware" / "src")
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, out)


def write_config(stage: Path, disabled_apps: set[str]) -> None:
    lines = [
        "#ifndef HK_CONFIG_H",
        "#define HK_CONFIG_H",
        '#define HACKYLENS_VERSION "0.1.0"',
    ]
    for app, flag in APP_MODULES.items():
        lines.append(f"#define {flag} {0 if app in disabled_apps else 1}")
    lines.extend([
        "#define HK_ENABLE_CAMERA_FEATURE (HK_ENABLE_APP_CAMERA || HK_ENABLE_APP_QR_CAMERA || HK_ENABLE_APP_FACE_DETECT)",
        "#define HK_ENABLE_QR_FEATURE HK_ENABLE_APP_QR_CAMERA",
        '#include "hk_config_default.h"',
        "#endif",
        "",
    ])
    (stage / "hk_config.h").write_text("\n".join(lines), encoding="utf-8")


def stage_target(sdk: Path, target_name: str, disabled_apps: set[str]) -> Path:
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

    camera_feature_enabled = ("camera" not in disabled_apps or
                              "qr-camera" not in disabled_apps or
                              "face-detect" not in disabled_apps)
    if camera_feature_enabled:
        quirc = ROOT / "firmware" / "third_party" / "quirc"
        for source in quirc.glob("*.c"):
            shutil.copy2(source, stage / source.name)
        for header in quirc.glob("*.h"):
            shutil.copy2(header, stage / header.name)

    write_config(stage, disabled_apps)
    return stage


def build_target(name: str, sdk: Path, toolchain_bin: Path, disabled_apps: set[str]) -> Path:
    target = TARGETS[name]
    project = str(target["project"])
    out_image = ROOT / "build" / str(target["output"])
    stage = stage_target(sdk, name, disabled_apps)
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
    run([cmake, "--build", cmake_path(build_dir)])

    built = build_dir / f"{project}.bin"
    if not built.is_file() or built.stat().st_size == 0:
        raise RuntimeError(f"build did not produce a non-empty image: {built}")
    if built.stat().st_size >= SETTINGS_FLASH_OFFSET:
        raise RuntimeError(
            f"firmware image reaches settings flash slot 0x{SETTINGS_FLASH_OFFSET:06X}: "
            f"{built.stat().st_size} bytes"
        )

    out_image.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built, out_image)
    run([sys.executable, str(ROOT / "tools" / "make_image.py"), str(out_image), "--out-dir", str(ROOT / "dist")])
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

    build_target(args.target, sdk, toolchain, set(args.disable_app))
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
