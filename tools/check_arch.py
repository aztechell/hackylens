#!/usr/bin/env python3
"""Check HackyLens firmware layer and feature-module boundaries."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware" / "src"
INCLUDE_RE = re.compile(r'^\s*#include\s+[<"]([^>"]+)[>"]')
SDK_HEADERS = {
    "bsp.h", "dmac.h", "dvp.h", "fpioa.h", "gpiohs.h", "kpu.h",
    "platform.h", "pwm.h", "sleep.h", "spi.h", "sysctl.h", "uart.h",
}
SDK_TOKEN_RE = re.compile(
    r"\b(dmac_|dvp_|fpioa_|gpiohs_|kpu_|pwm_|spi_|sysctl_|uart_|msleep|"
    r"sysctl_get_time_us|DVP_|FUNC_CMOS|FUNC_SCCB|FUNC_SPI|FPIOA_|"
    r"SPI_DEVICE|SPI_CHIP|SPI_WORK)"
)
PUBLIC_MUTABLE_EXTERN_RE = re.compile(
    r"^\s*extern\s+(?!const\b)(?!.*\()\S.*\s+\**[A-Za-z_]\w*"
    r"(?:\s*\[[^\]]*\])?\s*;"
)

FEATURES = {
    "terminal": ("terminal", "terminal_app.h"),
    "camera": ("camera", "camera_app.h"),
    "qr-camera": ("qr_camera", "qr_camera_app.h"),
    "face-detect": ("face_detect", "face_detect_app.h"),
    "apriltag": ("apriltag", "apriltag_app.h"),
    "files": ("files", "files_app.h"),
    "buttons": ("buttons", "buttons_app.h"),
    "pong": ("pong", "pong_app.h"),
    "settings": ("settings", "settings_app.h"),
    "sleep": ("sleep", "sleep_app.h"),
}
FEATURE_DIRS = {f"apps/{directory}/": (name, public)
                for name, (directory, public) in FEATURES.items()}
REGISTRY = "apps/app_registry.c"

LEGACY_PATHS = {
    "apps/app_terminal.c", "apps/app_terminal.h",
    "apps/app_camera.c", "apps/app_camera.h",
    "apps/app_qr_camera.c", "apps/app_qr_camera.h",
    "apps/app_face_detect.c", "apps/app_face_detect.h",
    "apps/app_files.c", "apps/app_files.h",
    "apps/app_buttons.c", "apps/app_buttons.h",
    "apps/app_pong.c", "apps/app_pong.h",
    "apps/app_settings.c", "apps/app_settings.h",
    "apps/app_sleep.c", "apps/app_sleep.h",
    "controllers/buttons_controller.c", "controllers/buttons_controller.h",
    "controllers/camera_photo_controller.c", "controllers/camera_photo_controller.h",
    "controllers/camera_photo_mode_controller.c", "controllers/camera_photo_mode_controller.h",
    "controllers/camera_settings_controller.c", "controllers/camera_settings_controller.h",
    "controllers/camera_settings_coordinator.c", "controllers/camera_settings_coordinator.h",
    "controllers/camera_settings_menu.c", "controllers/camera_settings_menu.h",
    "controllers/files_actions.c", "controllers/files_actions.h",
    "controllers/files_backend.c", "controllers/files_controller.c",
    "controllers/files_controller.h", "controllers/files_presenter.c",
    "controllers/files_presenter.h",
    "controllers/qr_camera_mode_controller.c", "controllers/qr_camera_mode_controller.h",
    "controllers/qr_result_controller.c", "controllers/qr_result_controller.h",
    "controllers/settings_app_menu.c", "controllers/settings_app_menu.h",
    "controllers/settings_controller.c", "controllers/settings_controller.h",
    "controllers/sleep_controller.c", "controllers/sleep_controller.h",
    "controllers/screen_controller.c", "controllers/screen_controller.h",
    "services/photo_service.c", "services/photo_service.h",
    "services/qr_camera_frame_adapter.c", "services/qr_camera_frame_adapter.h",
    "services/qr_decoder_engine.c", "services/qr_decoder_engine.h",
    "services/qr_luma.c", "services/qr_luma.h",
    "services/qr_result.h", "services/qr_result_state.c",
    "services/qr_service.c", "services/qr_service.h",
    "storage/file_browser_state.c", "storage/file_delete.c", "storage/file_delete.h",
    "storage/file_dir.c", "storage/file_dir.h",
    "storage/file_preview.c", "storage/file_preview.h",
    "storage/image_decode.h", "storage/image_decode_bmp.c",
    "storage/image_decode_common.c", "storage/image_decode_gif.c",
    "storage/image_decode_gif.h", "storage/image_decode_png.c",
    "storage/image_decode_png_inflate.c", "storage/image_decode_png_inflate.h",
    "storage/image_decode_ppm.c", "storage/image_decode_raw.c",
    "storage/image_viewer.c", "storage/image_viewer.h",
    "storage/photo_encode.c", "storage/photo_encode.h",
    "storage/photo_format.c", "storage/photo_format.h",
    "storage/photo_path.c", "storage/photo_path.h",
    "storage/photo_writer.c", "storage/photo_writer.h",
    "storage/qr_text_path.c", "storage/qr_text_path.h",
    "storage/qr_text_writer.c", "storage/qr_text_writer.h",
    "ui/buttons_view.c", "ui/buttons_view.h", "ui/files_view.c",
    "ui/qr_result_view.c", "ui/qr_result_view.h",
    "ui/sleep_view.c", "ui/sleep_view.h",
    "config/file_browser_config.h", "config/files_input_config.h",
    "config/files_layout.h", "config/image_decode_config.h",
    "config/photo_storage_config.h", "config/qr_layout.h",
    "core/files_view_port.h", "core/indexed_image.h",
}

FORBIDDEN_FILES = {
    "core/hk_settings.h", "core/hk_board_config.h", "core/hk_types.h",
    "services/camera_types.h", "services/camera_service.h",
    "services/camera_settings.h", "services/camera_input_state.h",
    "services/qr_result_state.h", "storage/hk_fat32.h",
    "services/camera_photo.h",
    "storage/file_types.h", "storage/file_view_bridge.h",
    "storage/file_browser_state.h", "storage/fat32_state_private.h",
    "services/settings_types.h", "apps/app_entrypoints.h",
    "config/settings_layout.h", "controllers/camera_settings_model.c",
    "controllers/camera_settings_model.h", "controllers/settings_actions.c",
    "controllers/settings_actions.h", "controllers/settings_model.c",
    "controllers/settings_model.h", "services/camera_settings_navigation.h",
    "ui/camera_settings_view.c", "ui/camera_settings_view.h",
    "ui/settings_view.c", "ui/settings_view.h",
}

SETTINGS_MENU_SHARED = {
    "config/settings_menu_layout.h",
    "controllers/settings_menu_controller.c",
    "controllers/settings_menu_controller.h",
    "ui/settings_menu_view.c",
    "ui/settings_menu_view.h",
}


def relative(path: Path) -> str:
    return path.relative_to(SRC).as_posix()


def includes(path: Path) -> list[tuple[int, str]]:
    result = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = INCLUDE_RE.match(line)
        if match:
            result.append((number, match.group(1)))
    return result


def resolve_include(path: Path, include: str) -> str | None:
    if include in SDK_HEADERS:
        return None
    for candidate in (path.parent / include, SRC / include):
        try:
            resolved = candidate.resolve()
            resolved.relative_to(SRC.resolve())
        except ValueError:
            continue
        if resolved.is_file():
            return resolved.relative_to(SRC.resolve()).as_posix()
    return None


def feature_for(path: str) -> tuple[str, str, str] | None:
    for prefix, (name, public) in FEATURE_DIRS.items():
        if path.startswith(prefix):
            return prefix, name, public
    return None


def layer_violation(path: str, include: str, target: str | None) -> str | None:
    if include in SDK_HEADERS and not path.startswith(("board/", "hal/")):
        return "SDK headers are limited to board/hal"
    if not path.startswith("runtime/") and target and target.startswith("runtime/"):
        return "only runtime may include runtime"
    if path.startswith("drivers/") and target and target.startswith(
            ("ui/", "apps/", "services/internal/", "storage/internal/")):
        return "drivers must not depend on UI, apps, or private state"
    if path.startswith("core/") and target and target.startswith(
            ("services/", "storage/", "ui/", "apps/", "controllers/")):
        return "core must not depend on upper layers"
    if path.startswith("storage/") and target and target.startswith(
            ("ui/", "apps/", "services/")):
        return "storage must not depend on UI, apps, or services"
    if path.startswith("services/") and target and target.startswith(
            ("ui/", "apps/")):
        return "services must not depend on UI or apps"
    if path.startswith("controllers/") and target and target.startswith(
            ("apps/", "services/internal/", "storage/internal/")):
        return "shared controllers must not depend on apps or private state"
    if path.startswith("ui/") and target and target.startswith(
            ("storage/", "services/")):
        return "shared UI must not depend on storage or services"
    if (path.startswith(("services/", "controllers/")) and
            path != "services/debug_console_service.c" and
            include == "../hal/hal_uart.h"):
        return "use the debug console service instead of debug UART"
    return None


def feature_include_violation(path: str, target: str | None) -> str | None:
    if not target:
        return None
    source_feature = feature_for(path)
    target_feature = feature_for(target)
    if not target_feature:
        return None
    target_prefix, target_name, target_public = target_feature
    if source_feature and source_feature[0] == target_prefix:
        return None
    if path == REGISTRY and target == f"{target_prefix}{target_public}":
        return None
    return f"only app registry may include the public {target_name} app header"


def settings_menu_violation(path: str, target: str | None) -> str | None:
    if target == "ui/settings_menu_view.h" and path not in {
            "controllers/settings_menu_controller.c", "ui/settings_menu_view.c"}:
        return "settings-menu view is private to its shared controller"
    if path not in SETTINGS_MENU_SHARED or not target:
        return None
    if target.startswith(("apps/", "services/", "storage/", "runtime/")) or "camera" in target:
        return "shared settings-menu must remain feature and service independent"
    return None


def include_cycle_failures() -> list[str]:
    graph: dict[str, list[str]] = {}
    for path in sorted(SRC.rglob("*.[ch]")):
        graph[relative(path)] = [
            target for _, include in includes(path)
            if (target := resolve_include(path, include))
        ]
    failures: list[str] = []
    visiting: set[str] = set()
    visited: set[str] = set()
    stack: list[str] = []

    def visit(node: str) -> None:
        if node in visiting:
            start = stack.index(node)
            failures.append("include cycle: " + " -> ".join(stack[start:] + [node]))
            return
        if node in visited:
            return
        visiting.add(node)
        stack.append(node)
        for child in graph.get(node, []):
            visit(child)
        stack.pop()
        visiting.remove(node)
        visited.add(node)

    for node in graph:
        visit(node)
    return failures


def layout_failures() -> list[str]:
    failures: list[str] = []
    manifest = (ROOT / "tools" / "build_firmware.py").read_text(encoding="utf-8")
    for path in sorted(LEGACY_PATHS | FORBIDDEN_FILES):
        if (SRC / path).exists():
            failures.append(f"{path}: legacy/forbidden path must not exist")
    for name, (directory, public) in FEATURES.items():
        module = SRC / "apps" / directory
        if not module.is_dir():
            failures.append(f"apps/{directory}: feature directory is missing")
        if not (module / public).is_file():
            failures.append(f"apps/{directory}/{public}: public app header is missing")
        marker = f'"{name}": Path("firmware/src/apps/{directory}")'
        if marker not in manifest:
            failures.append(f"tools/build_firmware.py: {name} is not directory-gated")
    for path in (SRC / "apps").glob("*.[ch]"):
        if path.name not in {"app_registry.c"}:
            failures.append(f"apps/{path.name}: flat app source is forbidden")
    if 'if "qr-camera" not in disabled_apps:' not in manifest:
        failures.append("tools/build_firmware.py: quirc is not gated by QR-CAMERA")
    if 'if "apriltag" not in disabled_apps:' not in manifest:
        failures.append("tools/build_firmware.py: AprilTag third party is not gated")
    return failures


def main() -> int:
    failures = layout_failures()
    for path in sorted(SRC.rglob("*.[ch]")):
        path_rel = relative(path)
        for number, include in includes(path):
            target = resolve_include(path, include)
            for violation in (
                layer_violation(path_rel, include, target),
                feature_include_violation(path_rel, target),
                settings_menu_violation(path_rel, target),
            ):
                if violation:
                    failures.append(f"{path_rel}:{number}: {violation}: {include}")
        if not path_rel.startswith(("board/", "hal/")):
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if line.lstrip().startswith("#include"):
                    continue
                if match := SDK_TOKEN_RE.search(line):
                    failures.append(f"{path_rel}:{number}: SDK token outside board/hal: {match.group(0)}")
                if PUBLIC_MUTABLE_EXTERN_RE.search(line):
                    failures.append(f"{path_rel}:{number}: public mutable extern variable")
    failures.extend(include_cycle_failures())
    if failures:
        print("[ARCH] boundary violations:")
        for failure in failures:
            print("  " + failure)
        return 1
    print("[OK] architecture boundary guard passed (10 declarative feature modules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
