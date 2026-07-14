#!/usr/bin/env python3
"""Check HackyLens firmware layer boundaries.

The allowlist records legacy couplings that still exist after the v58 port.
Future refactors should remove entries from ALLOWLIST, not add new ones.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware" / "src"
INCLUDE_RE = re.compile(r'^\s*#include\s+[<"]([^>"]+)[>"]')
SDK_HEADERS = {
    "bsp.h",
    "dmac.h",
    "dvp.h",
    "fpioa.h",
    "gpiohs.h",
    "kpu.h",
    "platform.h",
    "pwm.h",
    "sleep.h",
    "spi.h",
    "sysctl.h",
    "uart.h",
}
LOW_LEVEL_PREFIXES = ("board/", "hal/")
SDK_DRIVER_ALLOWLIST = set()

PONG_FEATURE_PREFIX = "apps/pong/"
PONG_REFERENCE_ALLOWED_PATHS = {"apps/app_registry.c"}
PONG_REFERENCE_RE = re.compile(r"\bpong(?:\b|_)")
PONG_SPECIFIC_FILE_RE = re.compile(r"^(?:app_pong|pong(?:_.*)?)\.[ch]$")
PONG_LEGACY_PATHS = {
    "firmware/src/apps/app_pong.c",
    "firmware/src/apps/app_pong.h",
    "firmware/src/controllers/pong_controller.c",
    "firmware/src/controllers/pong_controller.h",
    "firmware/src/ui/pong_view.c",
    "firmware/src/ui/pong_view.h",
}
PONG_ALLOWED_SHARED_INCLUDES = {
    "../../config/display_config.h",
    "../../config/input_config.h",
    "../../config/menu_layout.h",
    "../../core/hk_app.h",
    "../../core/hk_back_exit.h",
    "../../core/hk_menu.h",
    "../../core/hk_screen.h",
    "../../drivers/hk_lcd.h",
    "../../ui/hk_ui.h",
}
TERMINAL_FEATURE_PREFIX = "apps/terminal/"
TERMINAL_REFERENCE_ALLOWED_PATHS = {"apps/app_registry.c"}
TERMINAL_REFERENCE_RE = re.compile(r"\bterminal(?:\b|_)", re.IGNORECASE)
TERMINAL_SPECIFIC_FILE_RE = re.compile(r"^(?:app_terminal|terminal(?:_.*)?)\.[ch]$")
TERMINAL_LEGACY_PATHS = {
    "firmware/src/apps/app_terminal.c",
    "firmware/src/apps/app_terminal.h",
    "firmware/src/controllers/terminal_controller.c",
    "firmware/src/controllers/terminal_controller.h",
    "firmware/src/ui/ui_terminal.c",
}
TERMINAL_MODULE_FILES = {
    "terminal_app.c",
    "terminal_app.h",
    "terminal_controller.c",
    "terminal_controller.h",
    "terminal_view.c",
    "terminal_view.h",
    "terminal_buffer.c",
    "terminal_buffer.h",
    "terminal_config.h",
    "terminal_types.h",
}
TERMINAL_ALLOWED_SHARED_INCLUDES = {
    "../../config/display_config.h",
    "../../config/input_config.h",
    "../../core/hk_app.h",
    "../../core/hk_log.h",
    "../../core/hk_menu.h",
    "../../core/hk_screen.h",
    "../../drivers/hk_lcd.h",
    "../../services/settings_persistence.h",
    "../../services/settings_service.h",
}
DEBUG_SCREENSHOT_CONSOLE_API_RE = re.compile(r"\bdebug_uart_send_(bytes|text)\b")
SDK_TOKEN_RE = re.compile(
    r"\b(dmac_|dvp_|fpioa_|gpiohs_|kpu_|pwm_|spi_|sysctl_|uart_|msleep|sysctl_get_time_us|"
    r"DVP_|FUNC_CMOS|FUNC_SCCB|FUNC_SPI|FPIOA_|SPI_DEVICE|SPI_CHIP|SPI_WORK)"
)
STORAGE_INPUT_TOKEN_RE = re.compile(
    r"\b(BUTTON_(LEFT|RIGHT|OK|BACK)|FILES_OK_HOLD_US|FILES_REPEAT_(INITIAL|NEXT)_TICKS)\b"
)
PUBLIC_PHOTO_ROW_RE = re.compile(r"\bextern\s+uint8_t\s+g_photo_row\s*\[")
PUBLIC_MUTABLE_EXTERN_RE = re.compile(
    r"^\s*extern\s+(?!const\b)(?!.*\()\S.*\s+\**[A-Za-z_]\w*(?:\s*\[[^\]]*\])?\s*;"
)
FORBIDDEN_FILES = {
    "core/hk_settings.h": "settings service API must live in services/ narrow headers",
    "core/hk_board_config.h": "board compatibility facade was removed; include focused config headers",
    "core/hk_types.h": "type compatibility facade was removed; include focused type headers",
    "services/camera_types.h": "camera_types compatibility facade was removed; include core/camera_types.h",
    "services/camera_service.h": "camera service compatibility facade was removed; include narrow camera service headers",
    "services/camera_settings.h": "camera settings API is split into persist and navigation headers",
    "services/camera_input_state.h": "camera input state must be accessed through services/camera_input.h",
    "services/qr_result_state.h": "QR result state must be accessed through services/qr_result.h",
    "storage/hk_fat32.h": "FAT32 API is split into volume/allocation/directory/file/stream headers",
    "storage/file_types.h": "neutral file entry type belongs to core/file_types.h",
    "storage/file_view_bridge.h": "file view callback contract belongs to core/files_view_port.h",
    "storage/file_browser_state.h": "file browser state is storage-internal",
    "storage/fat32_state_private.h": "FAT32 private state is storage-internal",
    "services/settings_types.h": "settings payload format belongs to storage/settings_store_types.h",
    "apps/app_entrypoints.h": "app entrypoints must be declared by per-app headers",
}
FORBIDDEN_INCLUDE_TARGETS = set(FORBIDDEN_FILES)


ALLOWLIST = set()


def rel(path: Path) -> str:
    return path.relative_to(SRC).as_posix()


def include_lines(path: Path) -> list[tuple[int, str]]:
    lines: list[tuple[int, str]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = INCLUDE_RE.match(line)
        if match:
            lines.append((number, match.group(1)))
    return lines


def resolve_local_include(path: Path, inc: str) -> str | None:
    if inc in SDK_HEADERS:
        return None

    candidates = [
        path.parent / inc,
        SRC / inc,
    ]
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
            resolved.relative_to(SRC.resolve())
        except ValueError:
            continue
        if resolved.exists() and resolved.is_file():
            return resolved.relative_to(SRC.resolve()).as_posix()
    return None


def forbidden_include_reason(inc: str) -> str | None:
    normalized = inc.replace("\\", "/")
    if normalized.startswith("../"):
        parts = []
        for part in normalized.split("/"):
            if part == "..":
                continue
            parts.append(part)
        normalized = "/".join(parts)
    for target in FORBIDDEN_INCLUDE_TARGETS:
        if normalized == target or normalized.endswith("/" + target):
            return FORBIDDEN_FILES[target]
    return None


def classify_violation(path: str, inc: str) -> str | None:
    forbidden_reason = forbidden_include_reason(inc)
    if forbidden_reason:
        return f"forbidden compatibility/private include: {forbidden_reason}"

    if inc in SDK_HEADERS:
        if path.startswith(LOW_LEVEL_PREFIXES):
            return None
        return "SDK headers are limited to board/hal"

    if not path.startswith("runtime/") and (inc.startswith("../runtime/") or inc.startswith("runtime/")):
        return "only runtime may include runtime"

    if path.startswith("drivers/"):
        if inc.startswith("../ui/"):
            return "drivers must not include ui"
        if inc.startswith("../apps/"):
            return "drivers must not include apps"
        if inc.startswith("../services/internal/"):
            return "drivers must not include service internal headers"
        if inc.startswith("../storage/internal/"):
            return "drivers must not include storage internal headers"
        if inc.startswith("../storage/") and inc.endswith("_private.h"):
            return "drivers must not include storage private headers"
        if inc == "../core/hk_shell.h":
            return "drivers must not include shell"

    if path.startswith("core/"):
        if inc.startswith("../services/internal/"):
            return "core must not include service internal headers"
        if inc.startswith("../storage/internal/"):
            return "core must not include storage internal headers"
        if inc.startswith("../services/"):
            return "core must not include services"
        if inc.startswith("../storage/"):
            return "core must not include storage"
        if inc.startswith("../ui/"):
            return "core must not include ui"
        if inc.startswith("../apps/"):
            return "core must not include apps"
        if inc.startswith("../controllers/"):
            return "core must not include controllers"

    if path.startswith("storage/"):
        if inc == "../hal/hal_uart.h":
            return "storage must not include debug UART"
        if inc.startswith("../storage/internal/"):
            return "storage must include internal headers as internal/... from inside storage"
        if inc.startswith("../ui/"):
            return "storage must not include ui"
        if inc.startswith("../apps/"):
            return "storage must not include apps"
        if inc.startswith("../services/"):
            return "storage must not include services"
        if inc in ("../core/hk_menu.h", "../core/hk_screen.h"):
            return "storage must not include screen/menu controllers"
        if inc == "../core/hk_shell.h":
            return "storage must not include shell"
        if inc == "../drivers/hk_input.h":
            return "storage must not handle input directly"
        if inc.startswith("../drivers/") and inc.endswith("_private.h"):
            return "storage must not include driver private headers"

    if path.startswith("services/"):
        if inc.startswith("../storage/internal/"):
            return "services must not include storage internal headers"
        if inc.startswith("../services/internal/"):
            return "services must include internal headers as internal/... from inside services"
        if inc.startswith("../ui/"):
            return "services must not include ui"
        if inc.startswith("../apps/"):
            return "services must not include apps"
        if inc in ("../core/hk_menu.h", "../core/hk_screen.h", "../core/hk_shell.h"):
            return "services must not control screens directly"
        if inc.endswith("_private.h") and not inc.startswith("internal/"):
            return "services private-header include is temporary only"

    if path.startswith("controllers/"):
        if inc == "../hal/hal_uart.h":
            return "controllers must use debug console service instead of debug UART"
        if inc.startswith("../apps/"):
            return "controllers must not include apps"
        if inc.startswith("../services/internal/"):
            return "controllers must not include service internal headers"
        if inc.startswith("../storage/internal/"):
            return "controllers must not include storage internal headers"
        if inc.startswith("../drivers/") and inc.endswith("_private.h"):
            return "controllers must not include driver private headers"
        if inc.startswith("../storage/") and inc.endswith("_private.h"):
            return "controllers must not include storage private headers"

    if path.startswith("ui/"):
        if inc.startswith("../storage/"):
            return "ui must not include storage directly"
        if inc.startswith("../services/internal/"):
            return "ui must not include service internal headers"
        if inc.startswith("../services/"):
            return "ui must not include services"
        if inc.startswith("../drivers/") and inc.endswith("_private.h"):
            return "ui must not include driver private headers"
        if inc.startswith("../storage/") and inc.endswith("_private.h"):
            return "ui must not include storage private headers"

    if path.startswith(PONG_FEATURE_PREFIX) and inc.startswith("../../") and inc not in PONG_ALLOWED_SHARED_INCLUDES:
        return "Pong feature may include only declared shared contracts"

    if path.startswith("apps/"):
        if inc.startswith("../services/internal/"):
            return "apps must not include service internal headers"
        if inc.startswith("../storage/internal/"):
            return "apps must not include storage internal headers"
        if inc.startswith("../drivers/") and inc.endswith("_private.h"):
            return "apps must not include driver private headers"
        if inc.startswith("../storage/") and inc.endswith("_private.h"):
            return "apps must not include storage private headers"

    return None


def source_token_violations(path: Path) -> list[tuple[int, str]]:
    path_rel = rel(path)
    if path_rel.startswith(LOW_LEVEL_PREFIXES):
        return []

    violations: list[tuple[int, str]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.lstrip().startswith("#include"):
            continue
        match = SDK_TOKEN_RE.search(line)
        if match:
            violations.append((number, match.group(0)))
        if path_rel.startswith("storage/"):
            match = STORAGE_INPUT_TOKEN_RE.search(line)
            if match:
                violations.append((number, f"storage input token {match.group(0)}"))
        if PUBLIC_PHOTO_ROW_RE.search(line):
            violations.append((number, "public mutable image decode row buffer"))
        if PUBLIC_MUTABLE_EXTERN_RE.search(line):
            violations.append((number, "public mutable extern variable"))
        if path_rel == "services/debug_screenshot_stream.c":
            match = DEBUG_SCREENSHOT_CONSOLE_API_RE.search(line)
            if match:
                violations.append((number, "debug screenshot stream must not expose console API"))
    return violations


def feature_reference_violations(path: Path) -> list[tuple[int, str]]:
    path_rel = rel(path)
    violations: list[tuple[int, str]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if (not path_rel.startswith(PONG_FEATURE_PREFIX) and
                path_rel not in PONG_REFERENCE_ALLOWED_PATHS and
                PONG_REFERENCE_RE.search(line)):
            violations.append((number, "Pong reference outside feature module"))
        if (not path_rel.startswith(TERMINAL_FEATURE_PREFIX) and
                path_rel not in TERMINAL_REFERENCE_ALLOWED_PATHS and
                TERMINAL_REFERENCE_RE.search(line)):
            violations.append((number, "Terminal reference outside feature module"))
    return violations


def pong_include_violation(path: Path, inc: str) -> str | None:
    path_rel = rel(path)
    target = resolve_local_include(path, inc)

    if path_rel.startswith("apps/pong/pong_controller.") and Path(inc).name == "pong_app.h":
        return "Pong controller must not include the app entry-point header"
    if target and target.startswith(PONG_FEATURE_PREFIX):
        if path_rel.startswith(PONG_FEATURE_PREFIX):
            return None
        if path_rel == "apps/app_registry.c" and target in {
            "apps/pong/pong_app.h",
            "apps/pong/pong_config.h",
        }:
            return None
        return "shared layers must not include Pong feature headers"
    return None


def terminal_include_violation(path: Path, inc: str) -> str | None:
    path_rel = rel(path)
    target = resolve_local_include(path, inc)

    if path_rel.startswith("apps/terminal/terminal_controller.") and Path(inc).name == "terminal_app.h":
        return "Terminal controller must not include the app entry-point header"
    if target and target.startswith(TERMINAL_FEATURE_PREFIX):
        if path_rel.startswith(TERMINAL_FEATURE_PREFIX):
            return None
        if path_rel == "apps/app_registry.c" and target == "apps/terminal/terminal_app.h":
            return None
        return "shared layers must not include Terminal feature headers"
    if path_rel.startswith(TERMINAL_FEATURE_PREFIX) and inc.startswith("../../") and inc not in TERMINAL_ALLOWED_SHARED_INCLUDES:
        return "Terminal feature may include only declared shared contracts"
    return None


def pong_layout_failures() -> list[str]:
    failures: list[str] = []
    build_manifest = (ROOT / "tools" / "build_firmware.py").read_text(encoding="utf-8")

    for legacy_path in sorted(PONG_LEGACY_PATHS):
        if (ROOT / legacy_path).exists():
            failures.append(f"{legacy_path}: legacy Pong path must not exist")
        if legacy_path in build_manifest:
            failures.append(f"tools/build_firmware.py: legacy Pong source path: {legacy_path}")
    for path in sorted(SRC.rglob("*.[ch]")):
        path_rel = rel(path)
        if not path_rel.startswith(PONG_FEATURE_PREFIX) and PONG_SPECIFIC_FILE_RE.match(path.name):
            failures.append(f"{path_rel}: Pong-specific file must live in {PONG_FEATURE_PREFIX}")
    return failures


def terminal_layout_failures() -> list[str]:
    failures: list[str] = []
    build_manifest = (ROOT / "tools" / "build_firmware.py").read_text(encoding="utf-8")

    for legacy_path in sorted(TERMINAL_LEGACY_PATHS):
        if (ROOT / legacy_path).exists():
            failures.append(f"{legacy_path}: legacy Terminal path must not exist")
        if legacy_path in build_manifest:
            failures.append(f"tools/build_firmware.py: legacy Terminal source path: {legacy_path}")
    for name in sorted(TERMINAL_MODULE_FILES):
        path = SRC / TERMINAL_FEATURE_PREFIX / name
        manifest_path = f"firmware/src/{TERMINAL_FEATURE_PREFIX}{name}"
        if not path.is_file():
            failures.append(f"{path.relative_to(ROOT).as_posix()}: Terminal module file is missing")
        if manifest_path not in build_manifest:
            failures.append(f"tools/build_firmware.py: Terminal module is not disabled as a unit: {manifest_path}")
    for path in sorted(SRC.rglob("*.[ch]")):
        path_rel = rel(path)
        if not path_rel.startswith(TERMINAL_FEATURE_PREFIX) and TERMINAL_SPECIFIC_FILE_RE.match(path.name):
            failures.append(f"{path_rel}: Terminal-specific file must live in {TERMINAL_FEATURE_PREFIX}")
    return failures


def include_graph_failures() -> list[str]:
    graph: dict[str, list[str]] = {}
    for path in sorted(SRC.rglob("*.[ch]")):
        path_rel = rel(path)
        graph[path_rel] = []
        for _, inc in include_lines(path):
            target = resolve_local_include(path, inc)
            if target:
                graph[path_rel].append(target)

    failures: list[str] = []
    visiting: set[str] = set()
    visited: set[str] = set()
    stack: list[str] = []

    def dfs(node: str) -> None:
        if node in visiting:
            start = stack.index(node)
            failures.append("include cycle: " + " -> ".join(stack[start:] + [node]))
            return
        if node in visited:
            return
        visiting.add(node)
        stack.append(node)
        for child in graph.get(node, []):
            dfs(child)
        stack.pop()
        visiting.remove(node)
        visited.add(node)

    for node in sorted(graph):
        dfs(node)
    return failures


def main() -> int:
    failures: list[str] = []
    allowed = 0

    for forbidden, reason in sorted(FORBIDDEN_FILES.items()):
        if (SRC / forbidden).exists():
            failures.append(f"{forbidden}: forbidden file exists: {reason}")

    for path in sorted(SRC.rglob("*.[ch]")):
        path_rel = rel(path)
        for line_no, inc in include_lines(path):
            pong_violation = pong_include_violation(path, inc)
            if pong_violation:
                failures.append(f"{path_rel}:{line_no}: {pong_violation}: {inc}")
            terminal_violation = terminal_include_violation(path, inc)
            if terminal_violation:
                failures.append(f"{path_rel}:{line_no}: {terminal_violation}: {inc}")
            if path_rel.startswith("drivers/") and inc in SDK_HEADERS and (path_rel, inc) in SDK_DRIVER_ALLOWLIST:
                allowed += 1
            violation = classify_violation(path_rel, inc)
            if violation is None:
                continue
            allow_key = next((item for item in ALLOWLIST if item[0] == path_rel and item[1] == inc), None)
            if allow_key:
                allowed += 1
                continue
            failures.append(f"{path_rel}:{line_no}: {violation}: {inc}")
        for line_no, token in source_token_violations(path):
            failures.append(f"{path_rel}:{line_no}: SDK token outside low-level layer: {token}")
        for line_no, violation in feature_reference_violations(path):
            failures.append(f"{path_rel}:{line_no}: {violation}")

    failures.extend(pong_layout_failures())
    failures.extend(terminal_layout_failures())
    failures.extend(include_graph_failures())

    if failures:
        print("[ARCH] boundary violations:")
        for failure in failures:
            print("  " + failure)
        return 1

    print(f"[OK] architecture boundary guard passed ({allowed} allowlisted low-level exceptions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
