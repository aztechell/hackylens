#!/usr/bin/env python3
"""Validate Native App Manifests and emit one canonical build model."""

from __future__ import annotations

import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import tomllib
from typing import Any, Mapping, Sequence


SCHEMA_MAJOR = 1
APP_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
TOKEN_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
C_SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PATH_SEGMENT_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
LEGACY_SERVICE_PREFIX = "hackylens.service.legacy-"

REQUIRED_FIELDS = {
    "id", "name", "lifecycle", "entry", "sources", "requires", "tick_ms",
}
OPTIONAL_FIELDS = {
    "private_includes", "menu_order", "autostart_id", "optional", "debug",
}
ALLOWED_FIELDS = REQUIRED_FIELDS | OPTIONAL_FIELDS
LIFECYCLE_KINDS = {"legacy", "v2"}
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}

CAPABILITY_MINIMUM = "0.1.0"
CAPABILITY_MAXIMUM_EXCLUSIVE = "0.2.0"
DESCRIPTOR_VERSION = "0.1.0"
PLACEHOLDER_STATIC_RAM_BYTES = 1_048_576
PLACEHOLDER_STACK_BYTES = 16_384
PLACEHOLDER_STATE_BYTES = 262_144
V2_STATE_BYTES = 1_024
# Runtime presents with this deadline. 10 ms cannot finish a K210 320x240 SPI
# transfer; the display adapter and UI already use 500 ms.
PLACEHOLDER_RENDER_BUDGET_US = 500_000
DEFAULT_DEBUG = "No app-specific debug commands."

MAX_APP_ID_BYTES = 63
MAX_DISPLAY_NAME_BYTES = 96
MAX_DEBUG_BYTES = 1024
MAX_TICK_MS = 60_000
MAX_CAPABILITY_REQUESTS = 16
MAX_SERVICES = 16

# One build-time mapping from short names to existing Capability/service IDs.
# Feature sets preserve the current grant/composition ABI; they are not a
# second manifest. Per-app exceptions exist only where runtime acquire paths
# or optional composition currently differ.
CAPABILITY_IDS = {
    "display": "hackylens.cap.display",
    "input": "hackylens.cap.input",
    "time": "hackylens.cap.time",
    "lights": "hackylens.cap.lights",
    "external-link": "hackylens.cap.external-link",
}
SERVICE_IDS = {
    "camera": "hackylens.service.legacy-camera",
    "sd-card": "hackylens.service.legacy-sd-card",
    "internal-flash": "hackylens.service.legacy-internal-flash",
    "settings": "hackylens.service.settings",
}
OPTIONAL_FALLBACKS = {
    "display": "headless",
    "external-link": "hide-external-link-menu",
}
_DISPLAY_FEATURES = ("base-plane", "borrowed-surface", "dirty-regions", "rgb565")
_INPUT_FEATURES = ("debounced-buttons", "events", "state")
_TIME_SLEEP_UNTIL_APPS = frozenset({"apriltag", "camera", "micropython"})
_LIGHTS_FEATURES = {
    "settings": ("backlight", "illumination", "rgb"),
    "sleep": ("backlight",),
}
_EXTERNAL_LINK_FEATURES = {
    "micropython": ("i2c-controller", "uart"),
    "settings": ("i2c-target", "uart"),
}


class ManifestError(ValueError):
    """A Native App Manifest is malformed or unsafe."""


def canonical_json_bytes(document: object) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")


def generated_symbol(app_id: str) -> str:
    return "hk_generated_app_" + app_id.replace("-", "_")


def _mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ManifestError(f"{label}: expected table")
    return value


def _allowed(value: Any, allowed: set[str], required: set[str], label: str) -> Mapping[str, Any]:
    table = _mapping(value, label)
    unknown = sorted(set(table) - allowed)
    missing = sorted(required - set(table))
    if unknown or missing:
        details: list[str] = []
        if unknown:
            details.append("unknown=" + ",".join(unknown))
        if missing:
            details.append("missing=" + ",".join(missing))
        raise ManifestError(f"{label}: " + "; ".join(details))
    return table


def _string(
    value: Any,
    label: str,
    *,
    pattern: re.Pattern[str] | None = None,
    maximum_bytes: int | None = None,
) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise ManifestError(f"{label}: expected non-empty trimmed string")
    if any(ord(character) < 0x20 for character in value):
        raise ManifestError(f"{label}: control characters are forbidden")
    if pattern is not None and pattern.fullmatch(value) is None:
        raise ManifestError(f"{label}: invalid value {value!r}")
    if maximum_bytes is not None and len(value.encode("utf-8")) > maximum_bytes:
        raise ManifestError(f"{label}: exceeds {maximum_bytes} UTF-8 bytes")
    return value


def _integer(value: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ManifestError(f"{label}: expected integer")
    if value < minimum or value > maximum:
        raise ManifestError(f"{label}: outside {minimum}..{maximum}")
    return value


def _array(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ManifestError(f"{label}: expected array")
    return value


def _name_array(value: Any, label: str, *, allow_empty: bool) -> list[str]:
    raw = _array(value, label)
    if not raw:
        if allow_empty:
            return []
        raise ManifestError(f"{label}: must not be empty")
    names = [
        _string(item, f"{label}[{index}]", pattern=TOKEN_RE)
        for index, item in enumerate(raw)
    ]
    if len(names) != len(set(names)):
        raise ManifestError(f"{label}: duplicate value")
    return names


def _portable_parts(value: Any, label: str) -> tuple[str, ...]:
    text = _string(value, label)
    if "\\" in text or ":" in text:
        raise ManifestError(f"{label}: use a canonical forward-slash relative path")
    if PurePosixPath(text).is_absolute() or PureWindowsPath(text).is_absolute():
        raise ManifestError(f"{label}: absolute paths are forbidden")
    raw_parts = text.split("/")
    if any(
        not part or part in {".", ".."} or PATH_SEGMENT_RE.fullmatch(part) is None
        for part in raw_parts
    ):
        raise ManifestError(f"{label}: path is not canonical or contains traversal")
    return tuple(raw_parts)


def _resolve_existing(path: Path) -> Path:
    return path.resolve(strict=True)


def _resolve_app_path(
    app_directory: Path,
    value: Any,
    label: str,
    *,
    expected: str,
    suffixes: set[str] | None = None,
) -> str:
    parts = _portable_parts(value, label)
    cursor = app_directory
    for part in parts:
        try:
            names = {child.name for child in cursor.iterdir()}
        except OSError as exc:
            raise ManifestError(f"{label}: cannot inspect path: {exc}") from exc
        if part not in names:
            if any(name.casefold() == part.casefold() for name in names):
                raise ManifestError(f"{label}: path case does not match the filesystem")
            raise ManifestError(f"{label}: path does not exist")
        cursor = cursor / part
    try:
        app_real = _resolve_existing(app_directory)
        target_real = _resolve_existing(cursor)
        target_real.relative_to(app_real)
    except (OSError, ValueError) as exc:
        raise ManifestError(f"{label}: path escapes the real app directory") from exc
    if expected == "file" and not target_real.is_file():
        raise ManifestError(f"{label}: expected file")
    if expected == "directory" and not target_real.is_dir():
        raise ManifestError(f"{label}: expected directory")
    if suffixes is not None and target_real.suffix.casefold() not in suffixes:
        allowed = ", ".join(sorted(suffixes))
        raise ManifestError(f"{label}: expected one of {allowed}")
    return PurePosixPath(*parts).as_posix()


def _path_array(
    value: Any,
    label: str,
    app_directory: Path,
    *,
    expected: str,
    suffixes: set[str] | None = None,
    non_empty: bool,
) -> list[str]:
    raw = _array(value, label)
    if non_empty and not raw:
        raise ManifestError(f"{label}: must not be empty")
    if not non_empty and not raw:
        raise ManifestError(f"{label}: omit empty arrays")
    paths = [
        _resolve_app_path(
            app_directory,
            item,
            f"{label}[{index}]",
            expected=expected,
            suffixes=suffixes,
        )
        for index, item in enumerate(raw)
    ]
    folded = [item.casefold() for item in paths]
    if len(folded) != len(set(folded)):
        raise ManifestError(f"{label}: duplicate or case-colliding path")
    return sorted(paths, key=lambda item: (item.casefold(), item))


def _capability_features(name: str, app_id: str) -> tuple[str, ...]:
    if name == "display":
        return _DISPLAY_FEATURES
    if name == "input":
        return _INPUT_FEATURES
    if name == "time":
        if app_id in _TIME_SLEEP_UNTIL_APPS:
            return ("monotonic-us", "sleep-until")
        return ("monotonic-us",)
    if name == "lights":
        return _LIGHTS_FEATURES.get(app_id, ("illumination", "rgb"))
    if name == "external-link":
        return _EXTERNAL_LINK_FEATURES.get(app_id, ("i2c-controller", "uart"))
    raise ManifestError(f"unknown required service {name!r}")


def _capability_request(name: str, app_id: str, *, optional: bool) -> dict[str, Any]:
    request: dict[str, Any] = {
        "id": CAPABILITY_IDS[name],
        "instance": 0,
        "minimum": CAPABILITY_MINIMUM,
        "maximum_exclusive": CAPABILITY_MAXIMUM_EXCLUSIVE,
        "features": list(_capability_features(name, app_id)),
    }
    if optional:
        fallback = OPTIONAL_FALLBACKS.get(name)
        if fallback is None:
            raise ManifestError(f"optional {name!r} has no build-time fallback")
        request["fallback"] = fallback
    return request


def _service_request(name: str, app_id: str) -> dict[str, str]:
    service_id = SERVICE_IDS[name]
    suffix = service_id.split(".", 2)[-1]
    return {"id": service_id, "namespace": f"{app_id}.{suffix}"}


def _expand_names(
    names: Sequence[str],
    label: str,
    app_id: str,
    lifecycle: str,
    *,
    optional: bool,
) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    capabilities: list[dict[str, Any]] = []
    services: list[dict[str, str]] = []
    for name in names:
        if name in CAPABILITY_IDS:
            capabilities.append(
                _capability_request(name, app_id, optional=optional)
            )
            continue
        if name in SERVICE_IDS:
            if optional:
                raise ManifestError(f"{label}: services cannot be optional")
            if (
                SERVICE_IDS[name].startswith(LEGACY_SERVICE_PREFIX)
                and lifecycle != "legacy"
            ):
                raise ManifestError(
                    f"{label}: transitional legacy services require lifecycle=legacy"
                )
            services.append(_service_request(name, app_id))
            continue
        raise ManifestError(f"{label}: unknown required service {name!r}")
    capabilities.sort(key=lambda item: (item["id"], item["instance"]))
    services.sort(key=lambda item: (item["id"], item["namespace"]))
    return capabilities, services


def load_manifest(path: Path, scan_root: Path) -> dict[str, Any]:
    """Load one manifest and return its path-independent canonical model."""

    if path.name != "app.toml":
        raise ManifestError(f"{path}: manifest must be named app.toml")
    try:
        root_real = _resolve_existing(scan_root)
        manifest_real = _resolve_existing(path)
        directory_real = manifest_real.parent
        directory = directory_real.relative_to(root_real).as_posix() or "."
        with path.open("rb") as source:
            raw = tomllib.load(source)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as exc:
        raise ManifestError(f"cannot read manifest {path}: {exc}") from exc
    table = _allowed(raw, ALLOWED_FIELDS, REQUIRED_FIELDS, f"{path}: root")

    app_id = _string(
        table["id"], f"{path}: id", pattern=APP_ID_RE,
        maximum_bytes=MAX_APP_ID_BYTES,
    )
    name = _string(
        table["name"], f"{path}: name", maximum_bytes=MAX_DISPLAY_NAME_BYTES
    )
    lifecycle = _string(
        table["lifecycle"], f"{path}: lifecycle", pattern=TOKEN_RE
    )
    sources = _path_array(
        table["sources"], f"{path}: sources", directory_real,
        expected="file", suffixes=SOURCE_SUFFIXES, non_empty=True,
    )
    if "private_includes" in table:
        private_includes = _path_array(
            table["private_includes"], f"{path}: private_includes",
            directory_real, expected="directory", non_empty=False,
        )
    else:
        private_includes = []

    if "menu_order" in table:
        menu = {
            "visible": True,
            "order": _integer(table["menu_order"], f"{path}: menu_order", 1, 0xFFFF),
        }
    else:
        menu = {"visible": False, "order": 0}

    if "autostart_id" in table:
        autostart_id = _integer(
            table["autostart_id"], f"{path}: autostart_id", 0, 0xFFFF
        )
    else:
        autostart_id = 0
    autostart = {"eligible": autostart_id != 0, "id": autostart_id}

    tick_ms = _integer(table["tick_ms"], f"{path}: tick_ms", 1, MAX_TICK_MS)
    tick_interval_us = tick_ms * 1000
    # TIMER callbacks are measured against tick_budget_us. The legal maximum is
    # the tick interval; a 1 ms placeholder cannot host K210 TIMER + input.
    tick_budget_us = tick_interval_us

    required_names = _name_array(
        table["requires"], f"{path}: requires", allow_empty=True
    )
    optional_names = (
        _name_array(table["optional"], f"{path}: optional", allow_empty=False)
        if "optional" in table else []
    )
    overlap = sorted(set(required_names) & set(optional_names))
    if overlap:
        raise ManifestError(
            f"{path}: capability cannot be both required and optional"
        )
    required, required_services = _expand_names(
        required_names, f"{path}: requires", app_id, lifecycle, optional=False
    )
    optional, extra_services = _expand_names(
        optional_names, f"{path}: optional", app_id, lifecycle, optional=True
    )
    services = required_services + extra_services
    capability_keys = [(item["id"], item["instance"]) for item in required + optional]
    if len(capability_keys) != len(set(capability_keys)):
        raise ManifestError(
            f"{path}: capability cannot be both required and optional"
        )
    if len(capability_keys) > MAX_CAPABILITY_REQUESTS:
        raise ManifestError(
            f"{path}: capabilities exceed fixed runtime capacity "
            f"{MAX_CAPABILITY_REQUESTS}"
        )
    if len(services) > MAX_SERVICES:
        raise ManifestError(
            f"{path}: services exceed fixed runtime capacity {MAX_SERVICES}"
        )

    debug = (
        _string(
            table["debug"], f"{path}: debug", maximum_bytes=MAX_DEBUG_BYTES
        )
        if "debug" in table else DEFAULT_DEBUG
    )
    help_text = (
        f"{name} legacy feature app." if lifecycle == "legacy"
        else f"{name} feature app."
    )

    return {
        "directory": directory,
        "schema": SCHEMA_MAJOR,
        "id": app_id,
        "name": name,
        "version": DESCRIPTOR_VERSION,
        "entry": _string(table["entry"], f"{path}: entry", pattern=C_SYMBOL_RE),
        "generated_symbol": generated_symbol(app_id),
        "lifecycle": lifecycle,
        "sources": sources,
        "private_includes": private_includes,
        "menu": menu,
        "autostart": autostart,
        "capabilities": {"required": required, "optional": optional},
        "services": services,
        "limits": {
            "static_ram_bytes": PLACEHOLDER_STATIC_RAM_BYTES,
            "stack_bytes": PLACEHOLDER_STACK_BYTES,
            "state_bytes": (
                V2_STATE_BYTES if lifecycle == "v2" else PLACEHOLDER_STATE_BYTES
            ),
            "tick_interval_us": tick_interval_us,
            "tick_budget_us": tick_budget_us,
            "render_budget_us": PLACEHOLDER_RENDER_BUDGET_US,
        },
        "metadata": {"help": help_text, "debug": debug},
    }


def canonical_model(manifests: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Collision-check manifests and return their stable canonical ordering."""

    if not manifests:
        raise ManifestError("no app.toml manifests found")
    collision_fields = {
        "id": [(item["id"], item["id"]) for item in manifests],
        "entry": [(item["entry"], item["id"]) for item in manifests],
        "menu.order": [
            (item["menu"]["order"], item["id"])
            for item in manifests if item["menu"]["visible"]
        ],
        "autostart.id": [
            (item["autostart"]["id"], item["id"])
            for item in manifests if item["autostart"]["eligible"]
        ],
    }
    for field, values in collision_fields.items():
        owners: dict[Any, str] = {}
        for value, app_id in values:
            if value in owners:
                raise ManifestError(
                    f"collision for {field}={value!r}: {owners[value]!r}, {app_id!r}"
                )
            owners[value] = app_id
    for manifest in manifests:
        if manifest["lifecycle"] not in LIFECYCLE_KINDS:
            raise ManifestError(
                f"{manifest['id']}: lifecycle must be one of {sorted(LIFECYCLE_KINDS)}"
            )
    return {
        "schema": SCHEMA_MAJOR,
        "apps": sorted(manifests, key=lambda item: item["id"]),
    }


def validate_tree(scan_root: Path) -> dict[str, Any]:
    """Validate every app.toml below a root and return one canonical model."""

    if not scan_root.is_dir():
        raise ManifestError(f"scan root does not exist: {scan_root}")
    paths = sorted(scan_root.rglob("app.toml"), key=lambda item: item.as_posix())
    return canonical_model([load_manifest(path, scan_root) for path in paths])
